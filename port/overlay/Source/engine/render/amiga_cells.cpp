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
#include <vector>

#include <SDL.h>

extern "C" int msdl_env(const char *name, char *buf, int size);
extern "C" unsigned long amiga_eclock(void);
extern "C" unsigned long amiga_eclock_per_ms(void);
extern "C" void msdl_view_scene(int dx, int dy, int vw, int vh, const int *rects, int n, int full);
extern "C" void msdl_view_overlay(int x, int y, int w, int h);
extern "C" void msdl_view_overlay_all(void);
extern "C" void msdl_view_send_all(void);
extern "C" int msdl_view_take_damage(int *out4, int max);

namespace devilution::amiga_cells {

namespace {

constexpr int CellShift = 5;
constexpr int Cell = 1 << CellShift; // cell size in pixels
constexpr int GridW = 640 / Cell + 4; // with room
constexpr int GridH = 480 / Cell + 4;
constexpr int TileHalfW = 32; // TILE_WIDTH / 2
constexpr int TileHalfH = 16; // TILE_HEIGHT / 2
constexpr uint32_t HashInit = 0x811C9DC5U;

struct Grid {
	int bx = 0, by = 0; // world cell of grid (0, 0)
	uint32_t h[GridH][GridW];
	/** cells touched by a primitive the game clips inexactly at the view edge */
	uint8_t e[GridH][GridW];
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

int envFull = -1, envVerify = -1, envStats = -1, envNoGuard = -1, envNoSkip = -1;
bool noSkip = false;
int envClear = -1, envNoDirect = -1, envNoColumn = -1, envMapAll = -1, envVerifyMap = -1;
/** direct draw while the camera moves when the last frame had at least this share dirty (%) */
int directShare = 80;

// statistics
unsigned long stFrames, stFull, stDirty, stCells, stRects, stRecord, stRender, stCopy, stShift, stT0;
unsigned long vFrames, vBad;
unsigned long stTilesSkipped, stTilesVisited;
extern unsigned long stRecentre;
extern unsigned long stDirect;
unsigned long guardCount;

int FloorDiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/** floor(a / Cell): an arithmetic shift (no division on the 68060) */
inline int CellOf(int a)
{
	return a >> CellShift;
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

/** Margin bytes changed since the last check (and restores them). Reads
    only the margins: whole rows above and below, Margin bytes left and right. */
int CheckGuardRange(uint8_t *p, int n)
{
	int bad = 0;
	for (int i = 0; i < n; i++) {
		if (p[i] != GuardByte) {
			p[i] = GuardByte;
			bad++;
		}
	}
	return bad;
}

int CheckGuard(Guarded &g, const char *what)
{
	if (g.raw == nullptr)
		return 0;
	int above = 0, below = 0, left = 0, right = 0;
	for (int y = 0; y < Margin; y++)
		above += CheckGuardRange(g.raw + y * g.pitch, g.pitch);
	for (int y = Margin; y < Margin + g.h; y++) {
		uint8_t *row = g.raw + y * g.pitch;
		left += CheckGuardRange(row, Margin);
		right += CheckGuardRange(row + Margin + g.w, g.pitch - Margin - g.w);
	}
	for (int y = Margin + g.h; y < g.h + 2 * Margin; y++)
		below += CheckGuardRange(g.raw + y * g.pitch, g.pitch);
	const int bad = above + below + left + right;
	if (bad != 0)
		SDL_Log("cells: GUARD %s: bytes written outside the picture: %d above, %d below, %d left, %d right", what, above, below,
		    left, right);
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

/*
 * The scene lives in a window on a larger canvas (stage 2d): a camera move
 * moves the window, the pixels stay where they are. Only when the window
 * reaches the canvas edge is the view copied back to the middle.
 */
constexpr int CanvasPad = 256;
int originX = CanvasPad, originY = CanvasPad;
unsigned long stRecentre;

inline uint8_t *ScenePtr(int x, int y)
{
	return static_cast<uint8_t *>(scene->pixels) + (y + originY) * scene->pitch + x + originX;
}

Surface SceneSurface()
{
	return Surface(scene, SDL_Rect { originX, originY, viewW, viewH });
}

void ClearScene(const DirtyRect &r)
{
	for (int y = r.y; y < r.y + r.h; y++)
		std::memset(ScenePtr(r.x, y), 0, r.w);
}

/** new(x, y) = old(x + dx, y + dy): the window moves by (dx, dy). */
void MoveOrigin(int dx, int dy)
{
	const int nx = originX + dx, ny = originY + dy;
	if (nx >= 0 && ny >= 0 && nx <= 2 * CanvasPad && ny <= 2 * CanvasPad) {
		originX = nx;
		originY = ny;
		return;
	}
	// back to the middle: the part of the old view still visible
	const int w = viewW - (dx < 0 ? -dx : dx);
	const int h = viewH - (dy < 0 ? -dy : dy);
	uint8_t *p = static_cast<uint8_t *>(scene->pixels);
	const int pitch = scene->pitch;
	const int srcX = originX + (dx > 0 ? dx : 0), srcY = originY + (dy > 0 ? dy : 0);
	const int dstX = CanvasPad + (dx < 0 ? -dx : 0), dstY = CanvasPad + (dy < 0 ? -dy : 0);
	if (w > 0 && h > 0) {
		if (dstY <= srcY) {
			for (int y = 0; y < h; y++)
				std::memmove(p + (dstY + y) * pitch + dstX, p + (srcY + y) * pitch + srcX, w);
		} else {
			for (int y = h - 1; y >= 0; y--)
				std::memmove(p + (dstY + y) * pitch + dstX, p + (srcY + y) * pitch + srcX, w);
		}
	}
	originX = CanvasPad;
	originY = CanvasPad;
	stRecentre++;
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
					// primitive: when the camera moves, the clip line moves.
					// Only some primitives are inexact there (see-through walls
					// with a left / right mask, tiles cut at a corner -
					// RecordEdge): the cells they touch now or touched last frame
					// are drawn again; the other edge cells are reused.
					const bool moved = camX != prevCamX || camY != prevCamY;
					// reusable when the part visible now was visible last frame
					// (moved by the camera): also cells cut by the view edge
					const int px0 = cr.x + camX - prevCamX, py0 = cr.y + camY - prevCamY;
					const bool wasVisible = px0 >= 0 && py0 >= 0 && px0 + cr.w <= viewW && py0 + cr.h <= viewH;
					if (prevValid && wasVisible) {
						const int px = wcx - p.bx, py = wcy - p.by;
						if (px >= 0 && py >= 0 && px < GridW && py < GridH && p.h[py][px] == g.h[gy][gx]
						    && !(moved && (g.e[gy][gx] != 0 || p.e[py][px] != 0)))
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

bool gameCopyValid = false;
/** This frame's scene copy into the game's buffer was partial: what was drawn
    over the scene last frame (outside the damage list) is still there. */
bool lastCopyPartial = false;
unsigned long stPartial;
/** Automap stats: frames drawn, time, repairs, frames sent whole. */
unsigned long stMapFrames, stMapTime, stMapRepairs, stMapSendAll;
unsigned long stMapReplayed, stMapCaptured, stMapDirect, stMapVerified, stMapVerifyBad;
bool mapKeptLast = false; // last frame drew the automap and kept it on the game's buffer
bool mapKeptNow = false;
uint32_t mapHashLast = 0;
unsigned long mapT0, mapTLoop, mapTText, mapTPlr;
unsigned long stMapFrameReplays, stMapFrameCaptures, stMapFrameBad, stMapFrameLen;
unsigned long stMapInTile, stMapPre, stMapLoop, stMapText, stMapPlr, stMapPixels, stMapShown;
int lastDirtyShare = 0;
unsigned long stDirect;

/** Rectangle x, y, w, h of the scene into the game's buffer (clipped to the view). */
void CopyToGame(const Surface &out, int x, int y, int w, int h)
{
	int x1 = x + w, y1 = y + h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > viewW) x1 = viewW;
	if (y1 > viewH) y1 = viewH;
	for (int yy = y; yy < y1; yy++)
		std::memcpy(out.at(x, yy), ScenePtr(x, yy), x1 - x);
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
		const uint8_t *a = ScenePtr(0, y);
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
					std::fwrite(ScenePtr(0, y), 1, viewW, f);
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
		for (int y = 0; y < viewH; y++)
			std::memcpy(ScenePtr(0, y), static_cast<const uint8_t *>(verifyBuf->pixels) + y * verifyBuf->pitch, viewW);
		gameCopyValid = false;
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
	if (stTilesVisited != 0)
		SDL_Log("cells: render pass skipped %lu%% of %lu tile visits/frame", stTilesSkipped * 100 / stTilesVisited, stTilesVisited / f);
	stTilesSkipped = stTilesVisited = 0;
	SDL_Log("cells: %lu frames, %lu direct, %lu recentred, %lu full, %lu partial copies, dirty %lu%%, %lu rects/frame; ms/frame record %lu.%lu shift %lu.%lu render %lu.%lu copy %lu.%lu",
	    f, stDirect, stRecentre, stFull, stPartial, stCells != 0 ? stDirty * 100 / stCells : 0, stRects / f,
	    stRecord / perMs / f, (stRecord * 10 / perMs / f) % 10, stShift / perMs / f, (stShift * 10 / perMs / f) % 10,
	    stRender / perMs / f, (stRender * 10 / perMs / f) % 10, stCopy / perMs / f, (stCopy * 10 / perMs / f) % 10);
	if (stMapFrames != 0) {
		SDL_Log("cells: automap %lu frames, %lu.%lu ms/frame, %lu repairs, %lu sent whole; tiles/frame %lu replayed, %lu captured, %lu direct; verify %lu tiles, %lu differ",
		    stMapFrames, stMapTime / perMs / stMapFrames, (stMapTime * 10 / perMs / stMapFrames) % 10, stMapRepairs, stMapSendAll,
		    stMapReplayed / stMapFrames, stMapCaptured / stMapFrames, stMapDirect / stMapFrames, stMapVerified, stMapVerifyBad);
		SDL_Log("cells: automap whole-frame lists: %lu replayed, %lu captured, %lu not cached (%lu pixels in the last list)",
		    stMapFrameReplays, stMapFrameCaptures, stMapFrameBad, stMapFrameLen);
		stMapFrameReplays = stMapFrameCaptures = stMapFrameBad = 0;
		SDL_Log("cells: automap parts ms/frame: before the tiles (with the map check) %lu.%02lu, tiles %lu.%02lu, players and items %lu.%02lu, text and end %lu.%02lu",
		    stMapPre / perMs / stMapFrames, (stMapPre * 100 / perMs / stMapFrames) % 100,
		    stMapLoop / perMs / stMapFrames, (stMapLoop * 100 / perMs / stMapFrames) % 100,
		    stMapPlr / perMs / stMapFrames, (stMapPlr * 100 / perMs / stMapFrames) % 100,
		    stMapText / perMs / stMapFrames, (stMapText * 100 / perMs / stMapFrames) % 100);
	}
	stMapReplayed = stMapCaptured = stMapDirect = stMapVerified = stMapPre = stMapLoop = stMapText = stMapPlr = 0;
	stMapFrames = stMapTime = stMapRepairs = stMapSendAll = 0;
	stFrames = stFull = stDirty = stCells = stRects = stRecord = stRender = stCopy = stShift = stPartial = stRecentre = stDirect = 0;
	stT0 = now;
}

} // namespace

namespace {
constexpr int MaxTiles = 4096;
struct Box {
	int x0, y0, x1, y1;
};
Box tileBox[MaxTiles];
int curTile = -1;
bool tileOverflow = false;
} // namespace

void RecordPrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3)
{
	if (curTile >= 0 && curTile < MaxTiles) {
		Box &b = tileBox[curTile];
		if (x < b.x0) b.x0 = x;
		if (y < b.y0) b.y0 = y;
		if (x + w > b.x1) b.x1 = x + w;
		if (y + h > b.y1) b.y1 = y + h;
	}
	Grid &g = grids[cur];
	const int wx = x + camX, wy = y + camY;
	int cx0 = CellOf(wx) - g.bx;
	int cy0 = CellOf(wy) - g.by;
	int cx1 = CellOf(wx + w - 1) - g.bx;
	int cy1 = CellOf(wy + h - 1) - g.by;
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

void RecordEdge(int x, int y, int w, int h, bool masked)
{
	const bool cutH = x < 0 || x + w > viewW;
	const bool cutV = y < 0 || y + h > viewH;
	if (!((masked && (cutH || cutV)) || (cutH && cutV)))
		return;
	Grid &g = grids[cur];
	const int wx = x + camX, wy = y + camY;
	int cx0 = CellOf(wx) - g.bx, cy0 = CellOf(wy) - g.by;
	int cx1 = CellOf(wx + w - 1) - g.bx, cy1 = CellOf(wy + h - 1) - g.by;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 >= GridW) cx1 = GridW - 1;
	if (cy1 >= GridH) cy1 = GridH - 1;
	for (int cy = cy0; cy <= cy1; cy++)
		for (int cx = cx0; cx <= cx1; cx++)
			g.e[cy][cx] = 1;
}

namespace {
constexpr int CacheTiles = 112; // dungeon size (MAXDUNX / MAXDUNY)
struct ColumnEntry {
	uint32_t key;
	uint32_t hash;
	int16_t x0, y0, x1, y1; // box of the primitives, relative to the target position
	uint8_t valid;
	uint8_t masked;
	uint8_t empty;
};
ColumnEntry columnCache[2][CacheTiles][CacheTiles]; // [0] DrawCell, [1] DrawFloorTile
int capOX, capOY;
uint32_t capHash;
int capX0, capY0, capX1, capY1;
bool capMasked;
unsigned long stColumnHits, stColumnMisses;

void ClearColumnCache()
{
	for (auto &kind : columnCache)
		for (auto &row : kind)
			for (ColumnEntry &e : row)
				e.valid = 0;
}

void RecordColumn(const ColumnEntry &e, int ox, int oy)
{
	if (e.empty)
		return;
	const int x = ox + e.x0, y = oy + e.y0, w = e.x1 - e.x0, h = e.y1 - e.y0;
	RecordPrimitive(x, y, w, h, e.hash, 0xC011C0DEU, e.key);
	RecordEdge(x, y, w, h, e.masked != 0);
}
} // namespace

void CapturePrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, bool masked)
{
	const int rx = x - capOX, ry = y - capOY;
	capHash = Mix(Mix(Mix(Mix(Mix(Mix(capHash, k1), k2), k3), static_cast<uint32_t>(rx)), static_cast<uint32_t>(ry)), static_cast<uint32_t>((w << 16) ^ h));
	if (rx < capX0) capX0 = rx;
	if (ry < capY0) capY0 = ry;
	if (rx + w > capX1) capX1 = rx + w;
	if (ry + h > capY1) capY1 = ry + h;
	if (masked)
		capMasked = true;
}

bool ColumnReplay(int kind, const Surface &out, int tx, int ty, uint32_t key, int targetX, int targetY)
{
	if (tx < 0 || ty < 0 || tx >= CacheTiles || ty >= CacheTiles || Env(envNoColumn, "DIABLO_NOCOLUMN"))
		return false;
	const ColumnEntry &e = columnCache[kind & 1][tx][ty];
	if (!e.valid || e.key != key)
		return false;
	stColumnHits++;
	RecordColumn(e, targetX + out.region.x - sceneOX, targetY + out.region.y - sceneOY);
	return true;
}

void ColumnCaptureBegin(const Surface &out, int targetX, int targetY)
{
	capOX = targetX + out.region.x - sceneOX;
	capOY = targetY + out.region.y - sceneOY;
	capHash = HashInit;
	capX0 = capY0 = 0x7FFF;
	capX1 = capY1 = -0x8000;
	capMasked = false;
	columnCapture = true;
}

void ColumnCaptureEnd(int kind, int tx, int ty, uint32_t key)
{
	columnCapture = false;
	ColumnEntry e {};
	e.key = key;
	e.hash = capHash;
	e.empty = capX1 <= capX0 ? 1 : 0;
	e.x0 = static_cast<int16_t>(capX0);
	e.y0 = static_cast<int16_t>(capY0);
	e.x1 = static_cast<int16_t>(capX1);
	e.y1 = static_cast<int16_t>(capY1);
	e.masked = capMasked ? 1 : 0;
	e.valid = 1;
	stColumnMisses++;
	if (tx >= 0 && ty >= 0 && tx < CacheTiles && ty < CacheTiles)
		columnCache[kind & 1][tx][ty] = e;
	RecordColumn(e, capOX, capOY);
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

namespace {
const SDL_Surface *gameSurface = nullptr;
}

void RecordOverlayAbs(const SDL_Surface *surface, int x, int y, int w, int h)
{
	if (surface == gameSurface)
		msdl_view_overlay(x, y, w, h);
}

void OverlayAll()
{
	msdl_view_overlay_all();
}

void AutomapBegin(bool kept)
{
	// the map of the last frame is still on the game's buffer and the card:
	// gone or drawn differently now -> copy and send everything
	if (mapKeptLast && !kept)
		msdl_view_overlay_all();
	if (!kept)
		mapKeptLast = false;
	mapKeptNow = kept;
}

void AutomapDrawStart()
{
	mapHash = HashInit;
	mapT0 = Env(envStats, "DIABLO_STATS") ? amiga_eclock() : 0;
}

// ---- automap tiles: captured pixel lists, replayed (amiga_cells.hpp) ----
namespace {
constexpr int MapDim = 40;      // DMAXX, DMAXY
// cached tiles: map coordinates -32 .. 71, the whole loop of DrawAutomap
// (tiles beyond -2 / 41 see only empty neighbours and draw nothing; their
// colour reads the clamped AutomapView, within the 2-tile invalidation)
constexpr int MapPad = 32;
constexpr int MapCache = MapDim + 2 * MapPad;
constexpr uint32_t MapPool = 262144;
struct MapTileRef {
	uint32_t start;
	uint16_t count;
	uint16_t stamp;  // valid when == mapStamp
	int16_t x0, y0, x1, y1; // pixel box relative to the centre
};
MapTileRef mapTiles[MapCache][MapCache];
uint32_t mapPool[MapPool];
uint32_t mapPoolUsed = 0;
uint16_t mapStamp = 1;
uint32_t mapKeyLast = 0;
uint8_t mapViewSeen[MapDim * MapDim], mapDungeonSeen[MapDim * MapDim];
// tile being captured / verified
int mapCapState = 0; // 1 capture, 2 verify
int mapCapCx, mapCapCy;
uint32_t mapCapStart;
bool mapCapBad;
int16_t mapCapX0, mapCapY0, mapCapX1, mapCapY1;
MapTileRef *mapCapRef;
// any change of the cached tiles (flush, invalidation, capture)
uint32_t mapGen = 0;
// the whole tile loop of one frame: every pixel relative to the loop's first
// screen point, in drawing order; replayed while the loop's start tile and
// the tiles stay the same (walking: until the map moves by a tile)
constexpr uint32_t FrameMax = 32768;
int16_t frX[FrameMax], frY[FrameMax];
uint8_t frC[FrameMax];
uint32_t frCount = 0, frId = 0, frHashSaved, frCheck;
uint32_t frKey[5], frKeyNew[5];
bool frValid = false, frCapturing = false, frBad = false, frVerifying = false;
int frSX, frSY;

void FrameAppend(const MapTileRef &r, int cx, int cy)
{
	if (!frCapturing)
		return;
	if (frCount + r.count > FrameMax) {
		frBad = true;
		return;
	}
	const int bx = cx - frSX - 2048, by = cy - frSY - 2048;
	const uint32_t *p = mapPool + r.start;
	for (int n = r.count; n > 0; n--) {
		const uint32_t e = *p++;
		frX[frCount] = static_cast<int16_t>(bx + static_cast<int>(e >> 20));
		frY[frCount] = static_cast<int16_t>(by + static_cast<int>((e >> 8) & 0xFFF));
		frC[frCount] = static_cast<uint8_t>(e);
		frCount++;
	}
}

uint32_t FrameChecksum()
{
	uint32_t h = HashInit;
	for (uint32_t i = 0; i < frCount; i++)
		h = Mix(h, (static_cast<uint32_t>(static_cast<uint16_t>(frX[i])) << 16) ^ static_cast<uint16_t>(frY[i]) ^ (static_cast<uint32_t>(frC[i]) << 24));
	return Mix(h, frCount);
}

void MapFlush()
{
	mapGen++;
	if (++mapStamp == 0) {
		std::memset(mapTiles, 0, sizeof(mapTiles));
		mapStamp = 1;
	}
	mapPoolUsed = 0;
}

void MapInvalidateAround(int x, int y)
{
	mapGen++;
	for (int ty = y - 2; ty <= y + 2; ty++)
		for (int tx = x - 2; tx <= x + 2; tx++) {
			const int cx = tx + MapPad, cy = ty + MapPad;
			if (cx >= 0 && cy >= 0 && cx < MapCache && cy < MapCache)
				mapTiles[cx][cy].stamp = 0;
		}
}

void MapCheckArray(const uint8_t *now, uint8_t *seen)
{
	if (std::memcmp(now, seen, MapDim * MapDim) == 0)
		return;
	for (int i = 0; i < MapDim * MapDim; i++) {
		if (now[i] != seen[i]) {
			seen[i] = now[i];
			// [x][y] arrays: index = x * 40 + y
			MapInvalidateAround(i / MapDim, i % MapDim);
		}
	}
}
} // namespace

void MapTilesBegin(uint32_t key, const uint8_t *view, const uint8_t *dungeon)
{
	if (key != mapKeyLast) {
		mapKeyLast = key;
		MapFlush();
		std::memcpy(mapViewSeen, view, sizeof(mapViewSeen));
		std::memcpy(mapDungeonSeen, dungeon, sizeof(mapDungeonSeen));
	} else {
		MapCheckArray(view, mapViewSeen);
		MapCheckArray(dungeon, mapDungeonSeen);
	}
	if (mapT0 != 0)
		mapTLoop = amiga_eclock();
}

void MapCapturePixel(int x, int y, uint8_t color)
{
	const int dx = x - mapCapCx, dy = y - mapCapCy;
	if (dx < -2048 || dx > 2047 || dy < -2048 || dy > 2047 || mapPoolUsed >= MapPool) {
		mapCapBad = true;
	} else {
		mapPool[mapPoolUsed++] = (static_cast<uint32_t>(dx + 2048) << 20) | (static_cast<uint32_t>(dy + 2048) << 8) | color;
		if (dx < mapCapX0) mapCapX0 = static_cast<int16_t>(dx);
		if (dx > mapCapX1) mapCapX1 = static_cast<int16_t>(dx);
		if (dy < mapCapY0) mapCapY0 = static_cast<int16_t>(dy);
		if (dy > mapCapY1) mapCapY1 = static_cast<int16_t>(dy);
	}
	// the capture draws too (the caller's SetPixel)
}

namespace {
void MapReplay(const Surface &out, const MapTileRef &r, int cx, int cy)
{
	const int w = out.w(), h = out.h();
	if (r.count == 0 || cx + r.x1 < 0 || cy + r.y1 < 0 || cx + r.x0 >= w || cy + r.y0 >= h)
		return;
	uint8_t *const base = out.at(0, 0);
	const int pitch = out.pitch();
	const int bx = cx - 2048, by = cy - 2048;
	const uint32_t *p = mapPool + r.start;
	for (int n = r.count; n > 0; n--) {
		const uint32_t e = *p++;
		const int x = bx + static_cast<int>(e >> 20);
		const int y = by + static_cast<int>((e >> 8) & 0xFFF);
		if (static_cast<unsigned>(x) < static_cast<unsigned>(w) && static_cast<unsigned>(y) < static_cast<unsigned>(h))
			base[y * pitch + x] = static_cast<uint8_t>(e);
	}
}
} // namespace

int MapTile(const Surface &out, int cx, int cy, int mx, int my)
{
	const int ix = mx + MapPad, iy = my + MapPad;
	if (ix < 0 || iy < 0 || ix >= MapCache || iy >= MapCache) {
		stMapDirect++;
		frBad = true;
		return 2;
	}
	MapTileRef &r = mapTiles[ix][iy];
	const bool valid = r.stamp == mapStamp;
	if (valid) {
		MapReplay(out, r, cx, cy);
		FrameAppend(r, cx, cy);
		// same hash as when it was captured (see MapTileEnd)
		mapHash = Mix(Mix(Mix(mapHash, r.start ^ (static_cast<uint32_t>(r.count) << 20)), static_cast<uint32_t>(cx)), static_cast<uint32_t>(cy));
		stMapReplayed++;
		if (!Env(envVerifyMap, "DIABLO_VERIFY_MAP"))
			return 0;
		mapCapState = 2; // draw it again the old way and compare
	} else {
		mapCapState = 1;
	}
	mapCapCx = cx;
	mapCapCy = cy;
	mapCapStart = mapPoolUsed;
	mapCapBad = false;
	mapCapX0 = mapCapY0 = 0x7FFF;
	mapCapX1 = mapCapY1 = -0x8000;
	mapCapRef = &r;
	mapCapturing = true;
	return 1;
}

void MapTileEnd()
{
	mapCapturing = false;
	MapTileRef &r = *mapCapRef;
	const uint32_t count = mapPoolUsed - mapCapStart;
	if (mapCapState == 2) {
		stMapVerified++;
		const bool same = !mapCapBad && count == r.count && std::memcmp(mapPool + mapCapStart, mapPool + r.start, count * 4) == 0;
		mapPoolUsed = mapCapStart;
		if (!same) {
			stMapVerifyBad++;
			mapGen++;
			frBad = true;
			if (stMapVerifyBad <= 20)
				SDL_Log("cells: VERIFY MAP tile at %d,%d: %lu pixels now, %u cached", mapCapCx, mapCapCy, static_cast<unsigned long>(count), r.count);
			r.stamp = 0;
		}
	} else {
		stMapCaptured++;
		mapGen++;
		if (mapCapBad || count > 0xFFFF) {
			frBad = true;
			mapPoolUsed = mapCapStart;
			if (mapPoolUsed + 4096 >= MapPool)
				MapFlush(); // full: start again next tiles
			r.stamp = 0;
		} else {
			r.start = mapCapStart;
			r.count = static_cast<uint16_t>(count);
			r.x0 = mapCapX0;
			r.y0 = mapCapY0;
			r.x1 = mapCapX1;
			r.y1 = mapCapY1;
			r.stamp = mapStamp;
			FrameAppend(r, mapCapCx, mapCapCy);
		}
		mapHash = Mix(Mix(Mix(mapHash, r.start ^ (count << 20)), static_cast<uint32_t>(mapCapCx)), static_cast<uint32_t>(mapCapCy));
	}
	mapCapState = 0;
}

bool MapFrameBegin(const Surface &out, int sx, int sy, int mx, int my, int cells, bool opaque)
{
	frCapturing = false;
	if (!opaque) {
		frValid = false;
		return false;
	}
	frKeyNew[0] = static_cast<uint32_t>(mx);
	frKeyNew[1] = static_cast<uint32_t>(my);
	frKeyNew[2] = static_cast<uint32_t>(cells);
	frKeyNew[3] = mapGen;
	frKeyNew[4] = mapKeyLast;
	const bool same = frValid && std::memcmp(frKey, frKeyNew, sizeof(frKey)) == 0;
	frVerifying = same && Env(envVerifyMap, "DIABLO_VERIFY_MAP");
	if (same && !frVerifying) {
		const int w = out.w(), h = out.h();
		uint8_t *const base = out.at(0, 0);
		const int pitch = out.pitch();
		for (uint32_t i = 0; i < frCount; i++) {
			const int x = sx + frX[i], y = sy + frY[i];
			if (static_cast<unsigned>(x) < static_cast<unsigned>(w) && static_cast<unsigned>(y) < static_cast<unsigned>(h))
				base[y * pitch + x] = frC[i];
		}
		mapHash = Mix(Mix(Mix(mapHash, frId), static_cast<uint32_t>(sx)), static_cast<uint32_t>(sy));
		stMapFrameReplays++;
		return true;
	}
	if (frVerifying)
		frCheck = FrameChecksum();
	frCapturing = true;
	frBad = false;
	frCount = 0;
	frSX = sx;
	frSY = sy;
	frHashSaved = mapHash;
	return false;
}

void MapPlayersBegin()
{
	if (frCapturing) {
		frCapturing = false;
		if (frVerifying) {
			stMapVerified++;
			if (frBad || FrameChecksum() != frCheck) {
				stMapVerifyBad++;
				if (stMapVerifyBad <= 20)
					SDL_Log("cells: VERIFY MAP frame list differs (%lu pixels)", static_cast<unsigned long>(frCount));
			}
		}
		if (!frBad) {
			frValid = true;
			std::memcpy(frKey, frKeyNew, sizeof(frKey));
			frId++;
			// the hash a replay of this list gives (MapFrameBegin)
			mapHash = Mix(Mix(Mix(frHashSaved, frId), static_cast<uint32_t>(frSX)), static_cast<uint32_t>(frSY));
			stMapFrameCaptures++;
			stMapFrameLen = frCount;
		} else {
			frValid = false;
			stMapFrameBad++;
		}
	}
	if (mapT0 != 0)
		mapTPlr = amiga_eclock();
}

void MapTextBegin()
{
	if (mapT0 != 0)
		mapTText = amiga_eclock();
}

// ---- automap text (level, difficulty): its pixels cached (amiga_cells.hpp) ----
namespace {
Guarded textAG, textBG;
std::vector<uint32_t> mapText; // (x << 16) | (y << 8) | colour, in drawing area order
uint32_t mapTextKey = 0;
bool mapTextValid = false;
int mapTextX0, mapTextY0, mapTextX1, mapTextY1;
unsigned long stMapTextDraws, stMapTextBad;

/** The text drawn into two buffers cleared to 0x00 and 0xFF: a pixel the
    text wrote is the same in both. */
bool MapTextCapture(int w, int h, const std::function<void(const Surface &)> &draw, std::vector<uint32_t> &list,
    int &x0, int &y0, int &x1, int &y1)
{
	SDL_Surface *a = EnsureGuarded(textAG, w, h);
	SDL_Surface *b = EnsureGuarded(textBG, w, h);
	if (a == nullptr || b == nullptr)
		return false;
	for (int y = 0; y < h; y++) {
		std::memset(static_cast<uint8_t *>(a->pixels) + y * a->pitch, 0x00, w);
		std::memset(static_cast<uint8_t *>(b->pixels) + y * b->pitch, 0xFF, w);
	}
	draw(Surface(a));
	draw(Surface(b));
	list.clear();
	x0 = y0 = 0x7FFF;
	x1 = y1 = -1;
	for (int y = 0; y < h; y++) {
		const uint8_t *pa = static_cast<const uint8_t *>(a->pixels) + y * a->pitch;
		const uint8_t *pb = static_cast<const uint8_t *>(b->pixels) + y * b->pitch;
		for (int x = 0; x < w; x++) {
			if (pa[x] == pb[x]) {
				list.push_back((static_cast<uint32_t>(x) << 16) | (static_cast<uint32_t>(y) << 8) | pa[x]);
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
		}
	}
	return true;
}
} // namespace

bool MapTextCached(const Surface &out, uint32_t key, const std::function<void(const Surface &)> &draw)
{
	const int w = out.w(), h = std::min(out.h(), 128);
	key ^= static_cast<uint32_t>(w) * 2654435761U;
	const bool verify = Env(envVerifyMap, "DIABLO_VERIFY_MAP");
	if (!mapTextValid || key != mapTextKey) {
		if (!MapTextCapture(w, h, draw, mapText, mapTextX0, mapTextY0, mapTextX1, mapTextY1)) {
			mapTextValid = false;
			return false;
		}
		mapTextKey = key;
		mapTextValid = true;
		stMapTextDraws++;
	} else if (verify) {
		std::vector<uint32_t> check;
		int x0, y0, x1, y1;
		stMapVerified++;
		if (MapTextCapture(w, h, draw, check, x0, y0, x1, y1) && check != mapText) {
			stMapVerifyBad++;
			stMapTextBad++;
			if (stMapTextBad <= 20)
				SDL_Log("cells: VERIFY MAP text differs (%lu pixels now, %lu cached)", static_cast<unsigned long>(check.size()),
				    static_cast<unsigned long>(mapText.size()));
			mapText.swap(check);
			mapTextX0 = x0;
			mapTextY0 = y0;
			mapTextX1 = x1;
			mapTextY1 = y1;
		}
	}
	uint8_t *const base = out.at(0, 0);
	const int pitch = out.pitch();
	for (const uint32_t e : mapText)
		base[static_cast<int>((e >> 8) & 0xFF) * pitch + static_cast<int>(e >> 16)] = static_cast<uint8_t>(e);
	if (mapTextX1 >= mapTextX0)
		RecordOverlay(out, mapTextX0, mapTextY0, mapTextX1 - mapTextX0 + 1, mapTextY1 - mapTextY0 + 1);
	return true;
}

bool AutomapEnd(const Surface &out)
{
	const bool stats = mapT0 != 0;
	bool repair = false;
	if (!mapKeptNow) {
		// another map type: it covers the view, everything is sent (and
		// copied again next frame)
		msdl_view_overlay_all();
	} else {
		const bool changed = !mapKeptLast || mapHash != mapHashLast || camX != prevCamX || camY != prevCamY
		    || Env(envMapAll, "DIABLO_MAP_ALL");
		if (changed) {
			// pixels of the last map that the new one does not draw are still
			// on the game's buffer when its copy was partial: copy all of the
			// scene and draw the map again (the caller)
			if (mapKeptLast && lastCopyPartial) {
				for (int y = 0; y < viewH; y++)
					std::memcpy(out.at(0, y), ScenePtr(0, y), viewW);
				repair = true;
				stMapRepairs++;
			}
			msdl_view_send_all();
			stMapSendAll++;
		}
		mapHashLast = mapHash;
		mapKeptLast = true;
	}
	if (stats) {
		const unsigned long t = amiga_eclock();
		stMapFrames++;
		stMapTime += t - mapT0;
		if (mapTLoop != 0 && mapTText != 0) {
			stMapPre += mapTLoop - mapT0;
			stMapLoop += (mapTPlr != 0 ? mapTPlr : mapTText) - mapTLoop;
			if (mapTPlr != 0)
				stMapPlr += mapTText - mapTPlr;
			stMapText += t - mapTText;
		}
		mapTLoop = mapTText = mapTPlr = 0;
	}
	return repair;
}

SDL_Surface *TileScratch()
{
	return EnsureGuarded(scratchG, 64, 64);
}


bool BeginTileSlow()
{
	++curTile;
	if (curTile >= MaxTiles) {
		if (mode == Mode::Record)
			tileOverflow = true;
		return true;
	}
	if (mode == Mode::Record) {
		tileBox[curTile] = { 0x7FFF, 0x7FFF, -0x8000, -0x8000 };
		return true;
	}
	stTilesVisited++;
	if (tileOverflow || noSkip)
		return true;
	const Box &b = tileBox[curTile];
	for (int i = 0; i < numRects; i++) {
		const DirtyRect &r = rects[i];
		if (b.x0 < r.x + r.w && b.x1 > r.x && b.y0 < r.y + r.h && b.y1 > r.y)
			return true;
	}
	stTilesSkipped++;
	return false;
}

const DirtyRect *GetDirtyRects(int &count)
{
	count = numRects;
	return rects;
}

void DrawScene(const Surface &out, int firstTileX, int firstTileY, int offsetX, int offsetY, bool forceFull,
    uint32_t levelKey, bool lightMoves, const std::function<void(const Surface &)> &drawScene)
{
	overlayTrack = false;
	lastCopyPartial = false;
	gameSurface = out.surface;
	const bool stats = Env(envStats, "DIABLO_STATS");
	unsigned long t0 = stats ? amiga_eclock() : 0, t1, t2, t3, t4;

	viewW = out.w();
	viewH = out.h();
	scene = EnsureSurface(sceneG, scene, viewW + 2 * CanvasPad, viewH + 2 * CanvasPad);
	if (scene == nullptr || viewW > GridW * Cell - Cell || viewH > GridH * Cell - Cell) {
		drawScene(out);
		msdl_view_scene(0, 0, viewW, viewH, nullptr, 0, 1);
		overlayTrack = true;
		return;
	}
	if (scene->w != viewW + 2 * CanvasPad || levelKey != prevLevelKey)
		prevValid = false;
	if (levelKey != prevLevelKey)
		ClearColumnCache();
	prevLevelKey = levelKey;

	// camera: world pixel at the view's top left
	prevCamX = camX;
	prevCamY = camY;
	camX = (firstTileX - firstTileY) * TileHalfW - offsetX;
	camY = (firstTileX + firstTileY) * TileHalfH - offsetY;

	// Walking in the dungeon the player's light moves every game tick and
	// nearly every cell changes: then the cells only add the record pass and
	// the copy. While the camera moves and the last frame was (nearly) all
	// dirty, draw straight into the game's buffer as the game does; the first
	// frame with the camera still records again.
	// ... but only while the light keeps changing (a change in the last ~120 ms,
	// lighting.cpp): with the light-steps option it stays still between tiles
	// and the camera alone moves - then the cells reuse nearly everything.
	static uint32_t seenLightChanges, lightChangedMs;
	const uint32_t nowMs = SDL_GetTicks();
	if (lightChanges != seenLightChanges) {
		seenLightChanges = lightChanges;
		lightChangedMs = nowMs;
	}
	const bool lightRecent = lightMoves && nowMs - lightChangedMs < 120;
	if (lightRecent && (camX != prevCamX || camY != prevCamY) && lastDirtyShare >= directShare && !Env(envVerify, "DIABLO_VERIFY_RENDER")
	    && !Env(envNoDirect, "DIABLO_NODIRECT")) {
		static int d4[512 * 4];
		suppressSideEffects = false;
		drawScene(out);
		msdl_view_take_damage(d4, 512);
		msdl_view_scene(camX - prevCamX, camY - prevCamY, viewW, viewH, nullptr, 0, 1);
		prevValid = false;
		gameCopyValid = false;
		overlayTrack = true;
		if (stats) {
			stFrames++;
			stDirect++;
			stDirty += 100;
			stCells += 100;
			Stats(amiga_eclock());
		}
		return;
	}

	// 1. record
	cur ^= 1;
	Grid &g = grids[cur];
	g.bx = CellOf(camX);
	g.by = CellOf(camY);
	for (int y = 0; y < GridH; y++)
		for (int x = 0; x < GridW; x++)
			g.h[y][x] = HashInit;
	std::memset(g.e, 0, sizeof(g.e));
	sceneOX = originX;
	sceneOY = originY;
	curTile = -1;
	tileOverflow = false;
	noSkip = Env(envNoSkip, "DIABLO_NOSKIP");
	mode = Mode::Record;
	drawScene(SceneSurface());
	mode = Mode::Off;
	t1 = stats ? amiga_eclock() : 0;

	// 2. dirty cells
	const int dx = camX - prevCamX, dy = camY - prevCamY;
	if (dx >= viewW || -dx >= viewW || dy >= viewH || -dy >= viewH)
		prevValid = false;
	int visible;
	int dirty = BuildDirtyRects(visible);
	bool full = forceFull || Env(envFull, "DIABLO_FULL") || !prevValid || dirty * 10 > visible * 6;
	lastDirtyShare = visible > 0 ? (full ? 100 : dirty * 100 / visible) : 100;

	// 3. shift and draw
	if (full) {
		originX = CanvasPad;
		originY = CanvasPad;
	} else if (dx != 0 || dy != 0) {
		MoveOrigin(dx, dy);
	}
	sceneOX = originX;
	sceneOY = originY;
	t2 = stats ? amiga_eclock() : 0;
	suppressSideEffects = true;
	if (full) {
		fullRect = { 0, 0, viewW, viewH };
		ClearScene(fullRect);
		drawScene(SceneSurface());
		dirty = visible;
	} else if (numRects > 0) {
		// no clearing: every pixel of the view is drawn by some tile (the
		// verify mode compares with a full draw into a cleared buffer, so an
		// uncovered pixel would show there); ENV:DIABLO_CLEAR clears again
		if (Env(envClear, "DIABLO_CLEAR"))
			for (int i = 0; i < numRects; i++)
				ClearScene(rects[i]);
		traceGuard = Env(envVerify, "DIABLO_VERIFY_RENDER");
		curTile = -1;
		mode = Mode::Render;
		drawScene(SceneSurface());
		mode = Mode::Off;
		traceGuard = false;
	}
	suppressSideEffects = false;
	t3 = stats ? amiga_eclock() : 0;
	// margins: they take stray writes anyway; checked only when testing
	// (a periodic check in normal play caused 70 ms hitches)
	if (Env(envVerify, "DIABLO_VERIFY_RENDER")) {
		CheckGuard(sceneG, full ? "full draw" : "incremental draw");
		CheckGuard(scratchG, "tile scratch");
	}

	if (Env(envVerify, "DIABLO_VERIFY_RENDER"))
		Verify(drawScene);

	// test: RAM:cells-dump exists -> the scene and this frame's dirty
	// rectangles to RAM:cells.raw (tools/vdiff.py shows them), file deleted
	{
		static unsigned long lastCheck;
		const unsigned long now = SDL_GetTicks();
		if (now - lastCheck > 500) {
			lastCheck = now;
			if (FILE *t = std::fopen("RAM:cells-dump", "rb")) {
				std::fclose(t);
				std::remove("RAM:cells-dump");
				if (FILE *f = std::fopen("RAM:cells.raw", "wb")) {
					const uint8_t hdr[6] = { static_cast<uint8_t>(viewW >> 8), static_cast<uint8_t>(viewW), static_cast<uint8_t>(viewH >> 8),
						static_cast<uint8_t>(viewH), static_cast<uint8_t>(numRects >> 8), static_cast<uint8_t>(numRects) };
					std::fwrite(hdr, 1, 6, f);
					for (int pass = 0; pass < 2; pass++)
						for (int y = 0; y < viewH; y++)
							std::fwrite(ScenePtr(0, y), 1, viewW, f);
					for (int i = 0; i < numRects; i++) {
						const int16_t v[4] = { static_cast<int16_t>(rects[i].x), static_cast<int16_t>(rects[i].y), static_cast<int16_t>(rects[i].w), static_cast<int16_t>(rects[i].h) };
						for (int16_t e : v) {
							const uint8_t b[2] = { static_cast<uint8_t>(e >> 8), static_cast<uint8_t>(e) };
							std::fwrite(b, 1, 2, f);
						}
					}
					std::fclose(f);
					SDL_Log("cells: dumped the scene and %d dirty rectangles to RAM:cells.raw", numRects);
				}
			}
		}
	}

	// 4. the clean scene into the game's buffer (overlays are drawn on top of
	// it). With the camera still, the game's buffer differs from the new
	// scene only in the dirty rectangles and where something was drawn over
	// the scene since the last copy (stage 2c): only those are copied.
	{
		static int d4[512 * 4];
		static const uint8_t *lastGamePixels = nullptr;
		const int nd = msdl_view_take_damage(d4, 512);
		const uint8_t *gp = out.at(0, 0);
		const bool partial = !full && dx == 0 && dy == 0 && nd >= 0 && gp == lastGamePixels && gameCopyValid;
		lastGamePixels = gp;
		lastCopyPartial = partial;
		if (partial) {
			for (int i = 0; i < numRects; i++)
				CopyToGame(out, rects[i].x, rects[i].y, rects[i].w, rects[i].h);
			for (int i = 0; i < nd; i++)
				CopyToGame(out, d4[i * 4], d4[i * 4 + 1], d4[i * 4 + 2], d4[i * 4 + 3]);
			stPartial++;
		} else {
			for (int y = 0; y < viewH; y++)
				std::memcpy(out.at(0, y), ScenePtr(0, y), viewW);
		}
		gameCopyValid = true;
		// (with the automap kept on the game's buffer its pixels differ there on purpose)
		if (Env(envVerify, "DIABLO_VERIFY_RENDER") && !mapKeptLast) {
			for (int y = 0; y < viewH; y++) {
				if (std::memcmp(out.at(0, y), ScenePtr(0, y), viewW) != 0) {
					SDL_Log("cells: GAME COPY differs from the scene at row %d (%s copy)", y, partial ? "partial" : "full");
					for (int yy = 0; yy < viewH; yy++)
						std::memcpy(out.at(0, yy), ScenePtr(0, yy), viewW);
					break;
				}
			}
		}
	}
	prevValid = !forceFull;

	// to the present: camera move and changed scene rectangles
	{
		static int r4[GridW * GridH * 4];
		for (int i = 0; i < numRects; i++) {
			r4[i * 4] = rects[i].x;
			r4[i * 4 + 1] = rects[i].y;
			r4[i * 4 + 2] = rects[i].w;
			r4[i * 4 + 3] = rects[i].h;
		}
		msdl_view_scene(dx, dy, viewW, viewH, r4, full ? 0 : numRects, full ? 1 : 0);
	}
	overlayTrack = true;

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
