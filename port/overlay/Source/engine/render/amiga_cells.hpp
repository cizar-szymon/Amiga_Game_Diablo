/**
 * @file amiga_cells.hpp
 *
 * Warp port (AmigaOS): incremental scene renderer.
 *
 * The scene (DrawGame) is drawn into a scene buffer that keeps the last frame.
 * Every frame:
 *   1. "record" pass: the drawing primitives do not draw, they add their
 *      parameters and world position to a hash of every 32x32 world cell they
 *      cover (in drawing order);
 *   2. a cell whose hash changed, or that was not fully visible last frame,
 *      is dirty; the dirty cells become rectangles;
 *   3. the buffer is shifted by the camera move; "render" pass: every
 *      primitive is drawn clipped to the dirty rectangles it touches.
 * ENV:DIABLO_VERIFY_RENDER compares every frame with a full draw.
 * See docs/RENDERER_PLAN.md.
 */
#pragma once

#ifdef __amigaos__

#include <cstdint>
#include <cstring>
#include <functional>

#include "engine/surface.hpp"

namespace devilution::amiga_cells {

enum class Mode : uint8_t {
	Off,
	Record,
	Render,
};

inline Mode mode = Mode::Off;
/** Set while drawing again (render / verify passes): no side effects like the item label queue. */
inline bool suppressSideEffects = false;

struct DirtyRect {
	int x, y, w, h;
};

/**
 * Draws the scene (DrawGame's floor, tiles and sprites) into `out`, the view.
 * firstTile / offset: as DrawGame gets them (camera); drawScene(target) draws
 * the whole scene into target. forceFull: draw everything this frame and the
 * next. levelKey: changes when the level (its graphics) changes.
 */
void DrawScene(const Surface &out, int firstTileX, int firstTileY, int offsetX, int offsetY, bool forceFull,
    uint32_t levelKey, bool lightMoves, const std::function<void(const Surface &)> &drawScene);

/**
 * Overlays: everything drawn on the game's buffer outside the scene passes
 * (texts, health bars, cursor, boxes) reports its rectangle, so the present
 * knows which parts of the view to send again (msdl_view.c).
 */
inline bool overlayTrack = false;

/** Counts the game's light recalculations (lighting.cpp ProcessLightList): the
    scene is drawn directly only while the light keeps changing. */
inline uint32_t lightChanges = 0;
void RecordOverlayAbs(const SDL_Surface *surface, int x, int y, int w, int h);
inline void RecordOverlay(const Surface &out, int x, int y, int w, int h)
{
	if (overlayTrack)
		RecordOverlayAbs(out.surface, x + out.region.x, y + out.region.y, w, h);
}
/** Something covers / changes the whole view this frame (automap, panels, menus). */
void OverlayAll();

/**
 * Every tile visit of the scene (floor, tile content, out of bounds) starts
 * with BeginTile(). The record pass notes the area all primitives of the
 * visit cover; the render pass skips a visit whose area misses every dirty
 * rectangle - with all its game logic. Same order in both passes.
 */
bool BeginTileSlow();
inline bool BeginTile()
{
	return mode == Mode::Off ? true : BeginTileSlow();
}

/** Record pass: a primitive covering x,y,w,h (absolute scene coordinates). */
void RecordPrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3);
/** Render pass: the dirty rectangles (absolute scene coordinates). */
const DirtyRect *GetDirtyRects(int &count);

/** Test (verify mode): did the last draw write just above the scene? Logs who. */
void CheckTopGuard(const char *path, int ax, int ay, int w, int h, uint32_t k1, uint32_t k2, const DirtyRect &r);
inline bool traceGuard = false;
/** Where the scene window starts on the scene canvas (stage 2d): scene
    coordinates = buffer coordinates - origin. */
inline int sceneOX = 0, sceneOY = 0;

/**
 * Called at the start of every drawing primitive when mode != Off.
 * x, y, w, h: the area the primitive may touch, relative to `out`.
 * draw(sub, dx, dy): draws the primitive into `sub` with its position moved by (dx, dy).
 */
template <typename DrawFn>
inline void Intercept(const Surface &out, int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, DrawFn draw)
{
	// out's origin in scene coordinates
	const int orx = out.region.x - sceneOX;
	const int ory = out.region.y - sceneOY;
	const int ax = x + orx;
	const int ay = y + ory;
	if (mode == Mode::Record) {
		RecordPrimitive(ax, ay, w, h, k1, k2, k3);
		return;
	}
	int n;
	const DirtyRect *r = GetDirtyRects(n);
	mode = Mode::Off;
	for (int i = 0; i < n; i++) {
		const int x0 = ax > r[i].x ? ax : r[i].x;
		const int y0 = ay > r[i].y ? ay : r[i].y;
		const int x1 = (ax + w) < (r[i].x + r[i].w) ? (ax + w) : (r[i].x + r[i].w);
		const int y1 = (ay + h) < (r[i].y + r[i].h) ? (ay + h) : (r[i].y + r[i].h);
		if (x1 <= x0 || y1 <= y0)
			continue;
		// the dirty rectangle relative to `out`
		const int rx = r[i].x - orx;
		const int ry = r[i].y - ory;
		const Surface sub = out.subregion(rx, ry, r[i].w, r[i].h);
		draw(sub, -rx, -ry);
		if (traceGuard)
			CheckTopGuard("sprite", ax, ay, w, h, k1, k2, r[i]);
	}
	mode = Mode::Render;
}

/** Record pass only: the primitive at x, y (relative to `out`), without a draw closure. */
void RecordEdge(int x, int y, int w, int h, bool masked);

/**
 * Column cache (record pass): what DrawCell records for a tile depends only
 * on the tile, its dungeon piece, light level and wall transparency (the
 * key). Its primitives are kept as one summary (hash of all of them, their
 * bounding box) per tile; with the same key the next frame records that
 * summary alone and does not run DrawCell. While a column is captured,
 * RecordAt adds to the summary instead of recording.
 */
inline bool columnCapture = false;
void CapturePrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, bool masked);
/** true: the summary of tile (tx, ty) with this key was recorded (DrawCell not needed).
    kind 0: DrawCell, 1: DrawFloorTile. */
bool ColumnReplay(int kind, const Surface &out, int tx, int ty, uint32_t key, int targetX, int targetY);
void ColumnCaptureBegin(const Surface &out, int targetX, int targetY);
void ColumnCaptureEnd(int kind, int tx, int ty, uint32_t key);

inline void RecordAt(const Surface &out, int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, bool masked)
{
	const int ax = x + out.region.x - sceneOX, ay = y + out.region.y - sceneOY;
	if (columnCapture) {
		CapturePrimitive(ax, ay, w, h, k1, k2, k3, masked);
		return;
	}
	RecordPrimitive(ax, ay, w, h, k1, k2, k3);
	RecordEdge(ax, ay, w, h, masked);
}

/** Scratch surface (64x64, 8-bit) for InterceptTile. */
SDL_Surface *TileScratch();

/**
 * Like Intercept, for dungeon tile frames (at most 64x64). Their clipped
 * renderers are not exact inside the tile (the mask edge of see-through
 * walls moves when clipped left / right / at the top); at the screen edge
 * nobody sees it, inside the view a dirty rectangle edge would. So a tile
 * that is only partly inside a dirty rectangle is drawn whole (clipped only
 * by the scene, exactly as in a full draw) over a copy of the scene into a
 * scratch buffer, and only the part inside the rectangle is copied back.
 * inexactClip: left / right masked tiles always need it; other tiles only
 * when the rectangle cuts them on both sides horizontally.
 */
template <typename DrawFn>
inline void InterceptTile(const Surface &out, int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, bool inexactClip, DrawFn draw)
{
	// out's origin in scene coordinates
	const int orx = out.region.x - sceneOX;
	const int ory = out.region.y - sceneOY;
	const int ax = x + orx;
	const int ay = y + ory;
	if (mode == Mode::Record) {
		RecordPrimitive(ax, ay, w, h, k1, k2, k3);
		return;
	}
	// the tile clipped by `out`, as a full draw clips it (absolute coordinates)
	int ex0 = ax > orx ? ax : orx;
	int ey0 = ay > ory ? ay : ory;
	int ex1 = (ax + w) < (orx + out.region.w) ? (ax + w) : (orx + out.region.w);
	int ey1 = (ay + h) < (ory + out.region.h) ? (ay + h) : (ory + out.region.h);
	if (ex1 <= ex0 || ey1 <= ey0)
		return;
	int n;
	const DirtyRect *r = GetDirtyRects(n);
	mode = Mode::Off;
	for (int i = 0; i < n; i++) {
		const int x0 = ex0 > r[i].x ? ex0 : r[i].x;
		const int y0 = ey0 > r[i].y ? ey0 : r[i].y;
		const int x1 = ex1 < (r[i].x + r[i].w) ? ex1 : (r[i].x + r[i].w);
		const int y1 = ey1 < (r[i].y + r[i].h) ? ey1 : (r[i].y + r[i].h);
		if (x1 <= x0 || y1 <= y0)
			continue;
		// Directly, clipped by the rectangle: when the whole visible tile is
		// inside it, or (for exactly clipped tiles) when the rectangle cuts
		// at most one side horizontally - the triangle / trapezoid renderers
		// handle a left OR a right clip, never both (with both they write past
		// the right edge: wrong pixels, past the buffer end on the last row).
		// Also a horizontal cut combined with a vertical clip (the tile at the
		// top or bottom edge) goes wrong in the game's renderers: it writes
		// above the picture. Only cuts the full draw also makes go direct.
		const bool whole = x0 == ex0 && y0 == ey0 && x1 == ex1 && y1 == ey1;
		const bool hWhole = x0 == ex0 && x1 == ex1;
		const bool oneSide = x0 == ex0 || x1 == ex1;
		const bool vTileWhole = y0 == ay && y1 == ay + h;
		// the triangle renderers cannot clip top AND bottom either
		const bool vOneSide = y0 == ay || y1 == ay + h;
		if (whole || (!inexactClip && ((hWhole && vOneSide) || (oneSide && vTileWhole)))) {
			const int rx = r[i].x - orx;
			const int ry = r[i].y - ory;
			draw(out.subregion(rx, ry, r[i].w, r[i].h), -rx, -ry);
			if (traceGuard)
				CheckTopGuard("tile direct", ax, ay, w, h, k1, k2, r[i]);
			continue;
		}
		SDL_Surface *s = TileScratch();
		const int ew = ex1 - ex0, eh = ey1 - ey0;
		if (s == nullptr || ew > s->w || eh > s->h)
			continue;
		uint8_t *sp = static_cast<uint8_t *>(s->pixels);
		const int ox = ex0 - orx, oy = ey0 - ory; // in `out`
		{
			// rows with memcpy and the row addresses stepped by the pitch (the
			// compiler multiplied for every byte before)
			const int outPitch = out.pitch(), sPitch = s->pitch;
			const uint8_t *src = out.at(ox, oy);
			uint8_t *dst = sp;
			for (int yy = 0; yy < eh; yy++, src += outPitch, dst += sPitch)
				std::memcpy(dst, src, ew);
		}
		draw(Surface(s, SDL_Rect { 0, 0, ew, eh }), -ox, -oy);
		{
			const int outPitch = out.pitch(), sPitch = s->pitch, cw = x1 - x0;
			uint8_t *dst = out.at(x0 - orx, y0 - ory);
			const uint8_t *src = sp + (y0 - ey0) * sPitch + (x0 - ex0);
			for (int yy = y0; yy < y1; yy++, src += sPitch, dst += outPitch)
				std::memcpy(dst, src, cw);
		}
	}
	mode = Mode::Render;
}

inline uint32_t PtrKey(const void *p)
{
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

} // namespace devilution::amiga_cells

#endif // __amigaos__
