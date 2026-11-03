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
    uint32_t levelKey, const std::function<void(const Surface &)> &drawScene);

/**
 * Overlays: everything drawn on the game's buffer outside the scene passes
 * (texts, health bars, cursor, boxes) reports its rectangle, so the present
 * knows which parts of the view to send again (msdl_view.c).
 */
inline bool overlayTrack = false;
void RecordOverlayAbs(const SDL_Surface *surface, int x, int y, int w, int h);
inline void RecordOverlay(const Surface &out, int x, int y, int w, int h)
{
	if (overlayTrack)
		RecordOverlayAbs(out.surface, x + out.region.x, y + out.region.y, w, h);
}
/** Something covers / changes the whole view this frame (automap, panels, menus). */
void OverlayAll();

/** Record pass: a primitive covering x,y,w,h (absolute scene coordinates). */
void RecordPrimitive(int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3);
/** Render pass: the dirty rectangles (absolute scene coordinates). */
const DirtyRect *GetDirtyRects(int &count);

/** Test (verify mode): did the last draw write just above the scene? Logs who. */
void CheckTopGuard(const char *path, int ax, int ay, int w, int h, uint32_t k1, uint32_t k2, const DirtyRect &r);
inline bool traceGuard = false;

/**
 * Called at the start of every drawing primitive when mode != Off.
 * x, y, w, h: the area the primitive may touch, relative to `out`.
 * draw(sub, dx, dy): draws the primitive into `sub` with its position moved by (dx, dy).
 */
template <typename DrawFn>
inline void Intercept(const Surface &out, int x, int y, int w, int h, uint32_t k1, uint32_t k2, uint32_t k3, DrawFn draw)
{
	const int ax = x + out.region.x;
	const int ay = y + out.region.y;
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
		const int rx = r[i].x - out.region.x;
		const int ry = r[i].y - out.region.y;
		const Surface sub = out.subregion(rx, ry, r[i].w, r[i].h);
		draw(sub, -rx, -ry);
		if (traceGuard)
			CheckTopGuard("sprite", ax, ay, w, h, k1, k2, r[i]);
	}
	mode = Mode::Render;
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
	const int ax = x + out.region.x;
	const int ay = y + out.region.y;
	if (mode == Mode::Record) {
		RecordPrimitive(ax, ay, w, h, k1, k2, k3);
		return;
	}
	// the tile clipped by `out`, as a full draw clips it (absolute coordinates)
	int ex0 = ax > out.region.x ? ax : out.region.x;
	int ey0 = ay > out.region.y ? ay : out.region.y;
	int ex1 = (ax + w) < (out.region.x + out.region.w) ? (ax + w) : (out.region.x + out.region.w);
	int ey1 = (ay + h) < (out.region.y + out.region.h) ? (ay + h) : (out.region.y + out.region.h);
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
			const int rx = r[i].x - out.region.x;
			const int ry = r[i].y - out.region.y;
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
		const int ox = ex0 - out.region.x, oy = ey0 - out.region.y; // in `out`
		for (int yy = 0; yy < eh; yy++) {
			const uint8_t *src = out.at(ox, oy + yy);
			for (int xx = 0; xx < ew; xx++)
				sp[yy * s->pitch + xx] = src[xx];
		}
		draw(Surface(s, SDL_Rect { 0, 0, ew, eh }), -ox, -oy);
		for (int yy = y0; yy < y1; yy++) {
			uint8_t *dst = out.at(x0 - out.region.x, yy - out.region.y);
			const uint8_t *src = sp + (yy - ey0) * s->pitch + (x0 - ex0);
			for (int xx = 0; xx < x1 - x0; xx++)
				dst[xx] = src[xx];
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
