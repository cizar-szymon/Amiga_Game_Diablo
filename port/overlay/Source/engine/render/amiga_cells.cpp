/**
 * @file amiga_cells.cpp
 *
 * Warp port (AmigaOS): incremental scene renderer, see amiga_cells.hpp and
 * docs/RENDERER_PLAN.md.
 *
 * ENV:DIABLO_FULL           always draw the whole scene (A/B measurement)
 * ENV:DIABLO_VERIFY_RENDER  compare every frame with a full draw, log differences
 * ENV:DIABLO_STATS          every 5 s: dirty share, rectangles, time per pass
 */
#ifdef __amigaos__

#include "engine/render/amiga_cells.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include <SDL.h>

extern "C" int msdl_env(const char *name, char *buf, int size);
extern "C" unsigned long amiga_eclock(void);
extern "C" unsigned long amiga_eclock_per_ms(void);

namespace devilution::amiga_cells {

namespace {

constexpr int Cell = 32;
constexpr int GridW = 24; // 640 / 32 + 1, with room
constexpr int GridH = 16; // up to 480 / 32 + 1
constexpr int TileHalfW = 32; // TILE_WIDTH / 2
constexpr int TileHalfH = 16; // TILE_HEIGHT / 2
constexpr uint32_t HashInit = 0x811C9DC5U;

struct Grid {
	int bx = 0, by = 0; // world cell of grid (0, 0)
	uint32_t h[GridH][GridW];
};

Grid grids[2];
int cur = 0;
bool prevValid = false;
int camX, camY, prevCamX, prevCamY;
uint32_t prevLevelKey;
int viewW, viewH;

SDL_Surface *scene = nullptr;
SDL_Surface *verifyBuf = nullptr;

DirtyRect rects[GridW * GridH];
int numRects = 0;
DirtyRect fullRect;

int envFull = -1, envVerify = -1, envStats = -1;

// statistics
unsigned long stFrames, stFull, stDirty, stCells, stRects, stRecord, stRender, stCopy, stShift, stT0;
unsigned long vFrames, vBad;
unsigned long guardCount;

int FloorDiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

inline uint32_t Mix(uint32_t h, uint32_t v)
{
	h ^= v;
	return h * 0x01000193U;
}

bool Env(int &cache, const char *name)
{
	if (cache < 0) {
		char buf[8];
		cache = msdl_env(name, buf, sizeof(buf)) >= 0 ? 1 : 0;
		if (cache != 0)
			SDL_Log("cells: %s", name);
	}
	return cache != 0;
}

/*
 * Buffers with guard margins: 64 pixels on every side, filled with a known
 * byte. A primitive that writes outside the picture hits the margin instead
 * of the heap (the clipped renderers of the game are not exact everywhere);
 * CheckGuard() reports it.
 */
constexpr int Margin = 64;
constexpr uint8_t GuardByte = 0xA5;

struct Guarded {
	uint8_t *raw = nullptr;
	SDL_Surface *s = nullptr;
	int w = 0, h = 0, pitch = 0;
};

Guarded sceneG, verifyG, scratchG;

SDL_Surface *EnsureGuarded(Guarded &g, int w, int h)
{
	if (g.s != nullptr && g.w == w && g.h == h)
		return g.s;
	if (g.s != nullptr)
		SDL_FreeSurface(g.s);
	std::free(g.raw);
	g.s = nullptr;
	g.w = w;
	g.h = h;
	g.pitch = (w + 2 * Margin + 15) & ~15;
	g.raw = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(g.pitch) * (h + 2 * Margin) + 16));
	if (g.raw == nullptr)
		return nullptr;
	std::memset(g.raw, GuardByte, static_cast<size_t>(g.pitch) * (h + 2 * Margin) + 16);
	uint8_t *pixels = g.raw + Margin * g.pitch + Margin;
	for (int y = 0; y < h; y++)
		std::memset(pixels + y * g.pitch, 0, w);
	g.s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 8, g.pitch, SDL_PIXELFORMAT_INDEX8);
	return g.s;
}

/** Margin bytes changed since the last check (and restores them). */
int CheckGuard(Guarded &g, const char *what)
{
	if (g.raw == nullptr)
		return 0;
	int bad = 0, side = 0;
	const int rows = g.h + 2 * Margin;
	for (int y = 0; y < rows; y++) {
		uint8_t *row = g.raw + y * g.pitch;
		const bool inPicture = y >= Margin && y < Margin + g.h;
		for (int x = 0; x < g.pitch; x++) {
			if (inPicture && x >= Margin && x < Margin + g.w)
				continue;
			if (row[x] != GuardByte) {
				if (bad == 0)
					side = y < Margin ? 1 : (y >= Margin + g.h ? 2 : (x < Margin ? 3 : 4));
				bad++;
				row[x] = GuardByte;
			}
		}
	}
	if (bad != 0) {
		static const char *const sides[] = { "", "above", "below", "left of", "right of" };
		SDL_Log("cells: GUARD %s: %d bytes written %s the picture", what, bad, sides[side]);
	}
	return bad;
}

SDL_Surface *EnsureSurface(Guarded &g, SDL_Surface *, int w, int h)
{
	return EnsureGuarded(g, w, h);
}

void ClearRect(SDL_Surface *s, const DirtyRect &r)
{
	for (int y = r.y; y < r.y + r.h; y++)
		std::memset(static_cast<uint8_t *>(s->pixels) + y * s->pitch + r.x, 0, r.w);
}

/** new(x, y) = old(x + dx, y + dy) */
void ShiftScene(int dx, int dy)
{
	uint8_t *p = static_cast<uint8_t *>(scene->pixels);
	const int pitch = scene->pitch;
	const int w = viewW - (dx < 0 ? -dx : dx);
	const int dstX = dx < 0 ? -dx : 0;
	const int srcX = dx > 0 ? dx : 0;
	if (dy >= 0) {
		for (int y = 0; y + dy < viewH; y++)
			std::memmove(p + y * pitch + dstX, p + (y + dy) * pitch + srcX, w);
	} else {
		for (int y = viewH - 1; y + dy >= 0; y--)
			std::memmove(p + y * pitch + dstX, p + (y + dy) * pitch + srcX, w);
	}
}

/** Screen rectangle of world cell (wcx, wcy), clipped to the view; false when outside. */
bool CellRect(int wcx, int wcy, int cx, int cy, DirtyRect &r)
{
	int x0 = wcx * Cell - cx, y0 = wcy * Cell - cy;
	int x1 = x0 + Cell, y1 = y0 + Cell;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > viewW) x1 = viewW;
	if (y1 > viewH) y1 = viewH;
	if (x1 <= x0 || y1 <= y0)
		return false;
	r = { x0, y0, x1 - x0, y1 - y0 };
	return true;
}

/** Was world cell (wcx, wcy) fully on screen with the previous camera? */
bool WasFullyVisible(int wcx, int wcy)
{
	const int x0 = wcx * Cell - prevCamX, y0 = wcy * Cell - prevCamY;
	return x0 >= 0 && y0 >= 0 && x0 + Cell <= viewW && y0 + Cell <= viewH;
}

/** Bits: the cell touches the left / top / right / bottom edge of the view. */
unsigned EdgeBits(int wcx, int wcy, int cx, int cy)
{
	const int x0 = wcx * Cell - cx, y0 = wcy * Cell - cy;
	return (x0 <= 0 ? 1U : 0U) | (y0 <= 0 ? 2U : 0U) | (x0 + Cell >= viewW ? 4U : 0U) | (y0 + Cell >= viewH ? 8U : 0U);
}

/** Dirty cells -> rectangles (runs per row, merged with an identical run
    ending just above). Returns the number of dirty cells. */
int BuildDirtyRects(int &visibleCells)
{
	const Grid &g = grids[cur];
	const Grid &p = grids[cur ^ 1];
	int dirtyCount = 0;
	visibleCells = 0;
	numRects = 0;
	for (int gy = 0; gy < GridH; gy++) {
		bool inRun = false;
		DirtyRect run {};
		for (int gx = 0; gx <= GridW; gx++) {
			bool dirty = false;
			DirtyRect cr {};
			if (gx < GridW) {
				const int wcx = g.bx + gx, wcy = g.by + gy;
				if (CellRect(wcx, wcy, camX, camY, cr)) {
					visibleCells++;
					dirty = true;
					// The game's clipping at the view edge is not exact for every
					// primitive: when the camera moves, the clip line moves, so the
					// cells along the edges in that direction are drawn again.
					const unsigned edges = EdgeBits(wcx, wcy, camX, camY);
					const unsigned moved = (camX != prevCamX ? 5U : 0U) | (camY != prevCamY ? 10U : 0U);
					if (prevValid && WasFullyVisible(wcx, wcy) && (edges & moved) == 0) {
						const int px = wcx - p.bx, py = wcy - p.by;
						if (px >= 0 && py >= 0 && px < GridW && py < GridH && p.h[py][px] == g.h[gy][gx])
							dirty = false;
					}
				}
			}
			if (dirty) {
				dirtyCount++;
				if (!inRun) {
					run = cr;
					inRun = true;
				} else {
					run.w = cr.x + cr.w - run.x;
				}
				continue;
			}
			if (!inRun)
				continue;
			inRun = false;
			bool merged = false;
			for (int i = 0; i < numRects; i++) {
				DirtyRect &a = rects[i];
				if (a.x == run.x && a.w == run.w && a.y + a.h == run.y) {
					a.h += run.h;
					merged = true;
					break;
				}
			}
			if (!merged)
				rects[numRects++] = run;
		}
	}
	return dirtyCount;
}

void Verify(const std::function<void(const Surface &)> &drawScene)
{
	verifyBuf = EnsureSurface(verifyG, verifyBuf, viewW, viewH);
	if (verifyBuf == nullptr)
		return;
	const DirtyRect all { 0, 0, viewW, viewH };
	ClearRect(verifyBuf, all);
	mode = Mode::Off;
	suppressSideEffects = true;
	drawScene(Surface(verifyBuf));
	suppressSideEffects = false;
	CheckGuard(verifyG, "verify (full) draw");
	vFrames++;
	int bad = 0, fx = -1, fy = -1;
	for (int y = 0; y < viewH; y++) {
		const uint8_t *a = static_cast<const uint8_t *>(scene->pixels) + y * scene->pitch;
		const uint8_t *b = static_cast<const uint8_t *>(verifyBuf->pixels) + y * verifyBuf->pitch;
		if (std::memcmp(a, b, viewW) == 0)
			continue;
		for (int x = 0; x < viewW; x++) {
			if (a[x] != b[x]) {
				if (fx < 0) {
					fx = x;
					fy = y;
				}
				bad++;
			}
		}
	}
	if (bad != 0) {
		vBad++;
		if (vBad <= 3) {
			// both images for tools/vdiff.py: RAM:vfail<n>.raw = width, height (2 bytes
			// each), the incremental view, the full view, the dirty rectangles
			char name[32];
			std::snprintf(name, sizeof(name), "RAM:vfail%lu.raw", vBad);
			if (FILE *f = std::fopen(name, "wb")) {
				const uint8_t hdr[6] = { static_cast<uint8_t>(viewW >> 8), static_cast<uint8_t>(viewW), static_cast<uint8_t>(viewH >> 8),
					static_cast<uint8_t>(viewH), static_cast<uint8_t>(numRects >> 8), static_cast<uint8_t>(numRects) };
				std::fwrite(hdr, 1, 6, f);
				for (int y = 0; y < viewH; y++)
					std::fwrite(static_cast<const uint8_t *>(scene->pixels) + y * scene->pitch, 1, viewW, f);
				for (int y = 0; y < viewH; y++)
					std::fwrite(static_cast<const uint8_t *>(verifyBuf->pixels) + y * verifyBuf->pitch, 1, viewW, f);
				for (int i = 0; i < numRects; i++) {
					const int16_t v[4] = { static_cast<int16_t>(rects[i].x), static_cast<int16_t>(rects[i].y), static_cast<int16_t>(rects[i].w), static_cast<int16_t>(rects[i].h) };
					for (int16_t e : v) {
						const uint8_t b[2] = { static_cast<uint8_t>(e >> 8), static_cast<uint8_t>(e) };
						std::fwrite(b, 1, 2, f);
					}
				}
				std::fclose(f);
			}
		}
		if (vBad <= 30 || (vBad % 100) == 0)
			SDL_Log("cells: VERIFY frame %lu: %d pixels differ, first at %d,%d (cell %d,%d), %d rects, camera %d,%d moved %d,%d",
			    vFrames, bad, fx, fy, FloorDiv(fx + camX, Cell), FloorDiv(fy + camY, Cell), numRects, camX, camY,
			    camX - prevCamX, camY - prevCamY);
		// show the truth from now on, so one error is reported once
		std::memcpy(scene->pixels, verifyBuf->pixels, static_cast<size_t>(scene->pitch) * viewH);
	}
	if ((vFrames % 500) == 0)
		SDL_Log("cells: verify %lu frames, %lu with differences", vFrames, vBad);
}

void Stats(unsigned long now)
{
	const unsigned long perMs = amiga_eclock_per_ms();
	if (stT0 == 0)
		stT0 = now;
	if (now - stT0 < 5000 * perMs || stFrames == 0)
		return;
	const unsigned long f = stFrames;
	SDL_Log("cells: %lu frames, %lu full, dirty %lu%%, %lu rects/frame; ms/frame record %lu.%lu shift %lu.%lu render %lu.%lu copy %lu.%lu",
	    f, stFull, stCells != 0 ? stDirty * 100 / stCells : 0, stRects / f,
	    stRecord / perMs / f, (stRecord * 10 / perMs / f) % 10, stShift / perMs / f, (stShift * 10 / perMs / f) % 10,
	    stRender / perMs / f, (stRender * 10 / perMs / f) % 10, stCopy / perMs / f, (stCopy * 10 / perMs / f) % 10);
	stFrames = stFull = stDirty = stCells = stRects = stRecord = stRender = stCopy = stShift = 0;
	stT0 = now;
}

} // namespace

void RecordPrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3)
{
	Grid &g = grids[cur];
	const int wx = x + camX, wy = y + camY;
	int cx0 = FloorDiv(wx, Cell) - g.bx;
	int cy0 = FloorDiv(wy, Cell) - g.by;
	int cx1 = FloorDiv(wx + w - 1, Cell) - g.bx;
	int cy1 = FloorDiv(wy + h - 1, Cell) - g.by;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 >= GridW) cx1 = GridW - 1;
	if (cy1 >= GridH) cy1 = GridH - 1;
	if (cx1 < cx0 || cy1 < cy0)
		return;
	uint32_t key = Mix(Mix(Mix(Mix(Mix(HashInit, k1), k2), k3), static_cast<uint32_t>(wx)), static_cast<uint32_t>(wy));
	key = Mix(key, static_cast<uint32_t>((w << 16) ^ h));
	for (int cy = cy0; cy <= cy1; cy++)
		for (int cx = cx0; cx <= cx1; cx++)
			g.h[cy][cx] = Mix(g.h[cy][cx], key);
}

void CheckTopGuard(const char *path, int ax, int ay, int w, int h, uint32_t k1, uint32_t k2, const DirtyRect &r)
{
	static int reports = 0;
	if (sceneG.raw == nullptr)
		return;
	bool hit = false;
	for (int y = Margin - 8; y < Margin; y++) {
		uint8_t *row = sceneG.raw + y * sceneG.pitch;
		for (int x = 0; x < sceneG.pitch; x++) {
			if (row[x] != GuardByte) {
				hit = true;
				row[x] = GuardByte;
			}
		}
	}
	if (hit && reports < 20) {
		reports++;
		SDL_Log("cells: TOP WRITE by %s at %d,%d size %dx%d (k1 %08lx k2 %lx) rect %d,%d %dx%d",
		    path, ax, ay, w, h, static_cast<unsigned long>(k1), static_cast<unsigned long>(k2), r.x, r.y, r.w, r.h);
	}
}

SDL_Surface *TileScratch()
{
	return EnsureGuarded(scratchG, 64, 64);
}

const DirtyRect *GetDirtyRects(int &count)
{
	count = numRects;
	return rects;
}

void DrawScene(const Surface &out, int firstTileX, int firstTileY, int offsetX, int offsetY, bool forceFull,
    uint32_t levelKey, const std::function<void(const Surface &)> &drawScene)
{
	const bool stats = Env(envStats, "DIABLO_STATS");
	unsigned long t0 = stats ? amiga_eclock() : 0, t1, t2, t3, t4;

	viewW = out.w();
	viewH = out.h();
	scene = EnsureSurface(sceneG, scene, viewW, viewH);
	if (scene == nullptr || viewW > GridW * Cell - Cell || viewH > GridH * Cell - Cell) {
		drawScene(out);
		return;
	}
	if (scene->w != viewW || levelKey != prevLevelKey)
		prevValid = false;
	prevLevelKey = levelKey;

	// camera: world pixel at the view's top left
	prevCamX = camX;
	prevCamY = camY;
	camX = (firstTileX - firstTileY) * TileHalfW - offsetX;
	camY = (firstTileX + firstTileY) * TileHalfH - offsetY;

	// 1. record
	cur ^= 1;
	Grid &g = grids[cur];
	g.bx = FloorDiv(camX, Cell);
	g.by = FloorDiv(camY, Cell);
	for (int y = 0; y < GridH; y++)
		for (int x = 0; x < GridW; x++)
			g.h[y][x] = HashInit;
	mode = Mode::Record;
	drawScene(Surface(scene));
	mode = Mode::Off;
	t1 = stats ? amiga_eclock() : 0;

	// 2. dirty cells
	const int dx = camX - prevCamX, dy = camY - prevCamY;
	if (dx >= viewW || -dx >= viewW || dy >= viewH || -dy >= viewH)
		prevValid = false;
	int visible;
	int dirty = BuildDirtyRects(visible);
	bool full = forceFull || Env(envFull, "DIABLO_FULL") || !prevValid || dirty * 10 > visible * 6;

	// 3. shift and draw
	if (!full && (dx != 0 || dy != 0))
		ShiftScene(dx, dy);
	t2 = stats ? amiga_eclock() : 0;
	suppressSideEffects = true;
	if (full) {
		fullRect = { 0, 0, viewW, viewH };
		ClearRect(scene, fullRect);
		drawScene(Surface(scene));
		dirty = visible;
	} else if (numRects > 0) {
		for (int i = 0; i < numRects; i++)
			ClearRect(scene, rects[i]);
		traceGuard = Env(envVerify, "DIABLO_VERIFY_RENDER");
		mode = Mode::Render;
		drawScene(Surface(scene));
		mode = Mode::Off;
		traceGuard = false;
	}
	suppressSideEffects = false;
	t3 = stats ? amiga_eclock() : 0;
	if (Env(envVerify, "DIABLO_VERIFY_RENDER") || (++guardCount & 63) == 0) {
		CheckGuard(sceneG, full ? "full draw" : "incremental draw");
		CheckGuard(scratchG, "tile scratch");
	}

	if (Env(envVerify, "DIABLO_VERIFY_RENDER"))
		Verify(drawScene);

	// 4. the clean scene into the game's buffer (overlays are drawn on top of it)
	for (int y = 0; y < viewH; y++)
		std::memcpy(out.at(0, y), static_cast<const uint8_t *>(scene->pixels) + y * scene->pitch, viewW);
	prevValid = !forceFull;

	if (stats) {
		t4 = amiga_eclock();
		stFrames++;
		if (full)
			stFull++;
		stDirty += dirty;
		stCells += visible;
		stRects += full ? 1 : numRects;
		stRecord += t1 - t0;
		stShift += t2 - t1;
		stRender += t3 - t2;
		stCopy += t4 - t3;
		Stats(t4);
	}
}

} // namespace devilution::amiga_cells

#endif // __amigaos__
