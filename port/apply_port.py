#!/usr/bin/env python3
"""Applies the Warp port's changes to a clean copy of upstream DevilutionX.

Usage: apply_port.py <tree> <clean upstream tree>

Every change is an exact text replacement that must match once; the script
stops on anything unexpected, so a new upstream version shows at once what
no longer fits. Upstream keeps CRLF line endings: the texts below are
written with \\n and matched in the file's own line endings.
New files: port/overlay/<path> is copied over <tree>/<path>.
"""
import os
import shutil
import sys

CHANGES = [
    # The native SDL2 subset (port/sdl) instead of SDL2 from source.
    ("CMake/platforms/amiga.cmake",
     "set(DEVILUTIONX_SYSTEM_SDL2 OFF)\n",
     "# Warp port: the native SDL2 subset (port/sdl), found through SDL2_DIR.\n"
     "set(DEVILUTIONX_SYSTEM_SDL2 ON)\n"
     "set(DEVILUTIONX_STATIC_SDL2 OFF)\n"
     "# No threads: levels load on the main thread (interfac.cpp).\n"
     "add_compile_definitions(LOAD_ON_MAIN_THREAD)\n"
     "# Lighting per tile, as the original game: per-pixel lighting costs\n"
     "# 6.6 instead of 24 frames/s on a 68060 (docs/PORTING.md).\n"
     "set(DEFAULT_PER_PIXEL_LIGHTING false)\n"),
    # No renderer on the Amiga: the game draws straight into the 8-bit
    # window surface (dx.cpp CanRenderDirectlyToOutputSurface).
    ("Source/options.cpp",
     "#if defined(NXDK) || defined(__DJGPP__)\n          false\n#else\n          true\n#endif\n          )\n    , scaleQuality",
     "#if defined(NXDK) || defined(__DJGPP__) || defined(__amigaos__)\n          false\n#else\n          true\n#endif\n          )\n    , scaleQuality"),
    # The FPS counter on by default (beta builds; -f does the same).
    ("Source/options.cpp",
     'N_("Displays the FPS in the upper left corner of the screen."), false)',
     'N_("Displays the FPS in the upper left corner of the screen."),\n#ifdef __amigaos__\n          true\n#else\n          false\n#endif\n          )'),
    # Small copies of the tile renderer: gcc (m68k = strict alignment)
    # turns each memcpy(.., 4) into four byte moves through the stack; the
    # 68020+ reads and writes unaligned words and longs in hardware.
    ("Source/engine/render/overlapped_memset.hpp",
     "namespace devilution {\n",
     "namespace devilution {\n"
     "\n"
     "#ifdef __amigaos__\n"
     "namespace amiga_unaligned {\n"
     "typedef uint32_t __attribute__((may_alias)) U32;\n"
     "typedef uint16_t __attribute__((may_alias)) U16;\n"
     "DVL_ALWAYS_INLINE void Copy(void *d, const void *s, std::size_t n)\n"
     "{\n"
     "\tif (n == 4) {\n"
     "\t\t*static_cast<U32 *>(d) = *static_cast<const U32 *>(s);\n"
     "\t} else if (n == 8) {\n"
     "\t\tstatic_cast<U32 *>(d)[0] = static_cast<const U32 *>(s)[0];\n"
     "\t\tstatic_cast<U32 *>(d)[1] = static_cast<const U32 *>(s)[1];\n"
     "\t} else if (n == 2) {\n"
     "\t\t*static_cast<U16 *>(d) = *static_cast<const U16 *>(s);\n"
     "\t} else {\n"
     "\t\tstd::memcpy(d, s, n);\n"
     "\t}\n"
     "}\n"
     "} // namespace amiga_unaligned\n"
     "#define memcpy(d, s, n) ::devilution::amiga_unaligned::Copy((d), (s), (n))\n"
     "#endif\n"),
    ("Source/engine/render/overlapped_memset.hpp",
     "\n} // namespace devilution\n",
     "\n#ifdef __amigaos__\n#undef memcpy\n#endif\n\n} // namespace devilution\n"),
    # The two hottest dungeon loops in 68060 assembler (port/sdl/src/amiga_blit.c):
    # lit tiles 39%, see-through walls 22% of the time (profile 3.11.2026).
    ("Source/engine/render/blit_impl.hpp",
     "namespace devilution {\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" void amiga_blit_map(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map);\n"
     "extern \"C\" void amiga_blit_blend_map(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map, const unsigned char *pal);\n"
     "#endif\n"
     "\n"
     "namespace devilution {\n"),
    ("Source/engine/render/blit_impl.hpp",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n"
     "#ifdef __amigaos__\n"
     "\tamiga_blit_map(dst, src, length, colorMap);\n"
     "\treturn;\n"
     "#endif\n"),
    ("Source/engine/render/blit_impl.hpp",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsBlendedWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsBlendedWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n"
     "#ifdef __amigaos__\n"
     "\tamiga_blit_blend_map(dst, src, length, colorMap, &paletteTransparencyLookup[0][0]);\n"
     "\treturn;\n"
     "#endif\n"),
    # ---- incremental scene renderer (overlay engine/render/amiga_cells.*,
    # docs/RENDERER_PLAN.md) ----
    ("Source/CMakeLists.txt",
     "  engine/render/scrollrt.cpp\n",
     "  engine/render/scrollrt.cpp\n  engine/render/amiga_cells.cpp\n"),
    ("Source/engine/render/dun_render.cpp",
     "#include \"engine/render/blit_impl.hpp\"\n",
     "#include \"engine/render/blit_impl.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/engine/render/dun_render.cpp",
     "    MaskType maskType, const uint8_t *tbl)\n{\n#ifdef DEBUG_RENDER_OFFSET_X\n\tposition.x += DEBUG_RENDER_OFFSET_X;\n",
     "    MaskType maskType, const uint8_t *tbl)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_cells::mode != amiga_cells::Mode::Off) {\n"
     "\t\tamiga_cells::InterceptTile(out, position.x, position.y - height + 1, DunFrameWidth, height,\n"
     "\t\t    amiga_cells::PtrKey(src), (static_cast<uint32_t>(tile) << 8) | static_cast<uint32_t>(maskType), amiga_cells::PtrKey(tbl),\n"
     "\t\t    maskType == MaskType::Left || maskType == MaskType::Right,\n"
     "\t\t    [&](const Surface &sub, int dx, int dy) {\n"
     "\t\t\t    RenderTileFrame(sub, lightmap, Point { position.x + dx, position.y + dy }, tile, src, height, maskType, tbl);\n"
     "\t\t    });\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"
     "#ifdef DEBUG_RENDER_OFFSET_X\n\tposition.x += DEBUG_RENDER_OFFSET_X;\n"),
    ("Source/engine/render/dun_render.cpp",
     "void world_draw_black_tile(const Surface &out, int sx, int sy)\n{\n",
     "void world_draw_black_tile(const Surface &out, int sx, int sy)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_cells::mode != amiga_cells::Mode::Off) {\n"
     "\t\tamiga_cells::InterceptTile(out, sx, sy - TriangleHeight + 1, 2 * Width, TriangleHeight, 0xB1AC71E5U, 0, 0, false,\n"
     "\t\t    [&](const Surface &sub, int dx, int dy) { world_draw_black_tile(sub, sx + dx, sy + dy); });\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/engine/render/clx_render.cpp",
     "#include \"engine/render/blit_impl.hpp\"\n",
     "#include \"engine/render/blit_impl.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDraw(const Surface &out, Point position, ClxSprite clx)\n{\n",
     "#ifdef __amigaos__\n"
     "// Incremental renderer: area of the sprite (+ outline), its data and parameters.\n"
     "#define AMIGA_CELLS_CLX(K2, K3, EXTRA, ...) \\\n"
     "\tif (amiga_cells::mode != amiga_cells::Mode::Off) { \\\n"
     "\t\tamiga_cells::Intercept(out, position.x - (EXTRA), position.y - clx.height() + 1 - (EXTRA), clx.width() + 2 * (EXTRA), clx.height() + 2 * (EXTRA), \\\n"
     "\t\t    amiga_cells::PtrKey(clx.pixelData()), (K2), (K3), [&](const Surface &sub, int dx, int dy) { \\\n"
     "\t\t\t    const Point p { position.x + dx, position.y + dy }; \\\n"
     "\t\t\t    __VA_ARGS__; \\\n"
     "\t\t    }); \\\n"
     "\t\treturn; \\\n"
     "\t}\n"
     "#else\n"
     "#define AMIGA_CELLS_CLX(K2, K3, EXTRA, ...)\n"
     "#endif\n"
     "\n"
     "void ClxDraw(const Surface &out, Point position, ClxSprite clx)\n{\n"
     "\tAMIGA_CELLS_CLX(1, 0, 0, ClxDraw(sub, p, clx))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawTRN(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)\n{\n",
     "void ClxDrawTRN(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)\n{\n"
     "\tAMIGA_CELLS_CLX(2, amiga_cells::PtrKey(trn), 0, ClxDrawTRN(sub, p, clx, trn))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawWithLightmap(const Surface &out, Point position, ClxSprite clx, const Lightmap &lightmap)\n{\n",
     "void ClxDrawWithLightmap(const Surface &out, Point position, ClxSprite clx, const Lightmap &lightmap)\n{\n"
     "\tAMIGA_CELLS_CLX(3, 0, 0, ClxDrawWithLightmap(sub, p, clx, lightmap))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawBlended(const Surface &out, Point position, ClxSprite clx)\n{\n",
     "void ClxDrawBlended(const Surface &out, Point position, ClxSprite clx)\n{\n"
     "\tAMIGA_CELLS_CLX(4, 0, 0, ClxDrawBlended(sub, p, clx))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawBlendedTRN(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)\n{\n",
     "void ClxDrawBlendedTRN(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)\n{\n"
     "\tAMIGA_CELLS_CLX(5, amiga_cells::PtrKey(trn), 0, ClxDrawBlendedTRN(sub, p, clx, trn))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawBlendedWithLightmap(const Surface &out, Point position, ClxSprite clx, const Lightmap &lightmap)\n{\n",
     "void ClxDrawBlendedWithLightmap(const Surface &out, Point position, ClxSprite clx, const Lightmap &lightmap)\n{\n"
     "\tAMIGA_CELLS_CLX(6, 0, 0, ClxDrawBlendedWithLightmap(sub, p, clx, lightmap))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawOutline(const Surface &out, uint8_t col, Point position, ClxSprite clx)\n{\n",
     "void ClxDrawOutline(const Surface &out, uint8_t col, Point position, ClxSprite clx)\n{\n"
     "\tAMIGA_CELLS_CLX(7, col, 1, ClxDrawOutline(sub, col, p, clx))\n"),
    ("Source/engine/render/clx_render.cpp",
     "void ClxDrawOutlineSkipColorZero(const Surface &out, uint8_t col, Point position, ClxSprite clx)\n{\n",
     "void ClxDrawOutlineSkipColorZero(const Surface &out, uint8_t col, Point position, ClxSprite clx)\n{\n"
     "\tAMIGA_CELLS_CLX(8, col, 1, ClxDrawOutlineSkipColorZero(sub, col, p, clx))\n"),
    ("Source/engine/render/scrollrt.cpp",
     "#include \"engine/render/scrollrt.h\"\n",
     "#include \"engine/render/scrollrt.h\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/engine/render/scrollrt.cpp",
     "\tLightmap lightmap = Lightmap::build(*GetOptions().Graphics.perPixelLighting, position, Point {} + offset,\n"
     "\t    gnScreenWidth, gnViewportHeight, rows, columns,\n"
     "\t    out.at(0, 0), out.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable,\n"
     "\t    dLight, MicroTileLen);\n"
     "\n"
     "\tDrawFloor(out, lightmap, position, Point {} + offset, rows, columns);\n"
     "\tDrawTileContent(out, lightmap, position, Point {} + offset, rows, columns);\n"
     "\tDrawOOB(out, lightmap, position, Point {} + offset, rows, columns);\n",
     "\tconst auto drawScene = [&](const Surface &target) {\n"
     "\t\tLightmap lightmap = Lightmap::build(*GetOptions().Graphics.perPixelLighting, position, Point {} + offset,\n"
     "\t\t    gnScreenWidth, gnViewportHeight, rows, columns,\n"
     "\t\t    target.at(0, 0), target.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable,\n"
     "\t\t    dLight, MicroTileLen);\n"
     "\n"
     "\t\tDrawFloor(target, lightmap, position, Point {} + offset, rows, columns);\n"
     "\t\tDrawTileContent(target, lightmap, position, Point {} + offset, rows, columns);\n"
     "\t\tDrawOOB(target, lightmap, position, Point {} + offset, rows, columns);\n"
     "\t};\n"
     "#ifdef __amigaos__\n"
     "\tif (!*GetOptions().Graphics.zoom) {\n"
     "\t\t// Warp port: only the changed cells (engine/render/amiga_cells.cpp)\n"
     "\t\tconst uint32_t levelKey = (static_cast<uint32_t>(currlevel) | (static_cast<uint32_t>(setlvlnum) << 8)\n"
     "\t\t                             | (static_cast<uint32_t>(leveltype) << 16) | (setlevel ? 0x1000000U : 0U))\n"
     "\t\t    ^ amiga_cells::PtrKey(pDungeonCels.get());\n"
     "\t\tamiga_cells::DrawScene(out, position.x, position.y, offset.deltaX, offset.deltaY,\n"
     "\t\t    *GetOptions().Graphics.perPixelLighting, levelKey, leveltype != DTYPE_TOWN, drawScene);\n"
     "\t} else\n"
     "#endif\n"
     "\t\tdrawScene(out);\n"),
    ("Source/qol/itemlabels.cpp",
     "#include \"itemlabels.h\"\n",
     "#include \"itemlabels.h\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/qol/itemlabels.cpp",
     "void AddItemToLabelQueue(int id, Point position)\n{\n",
     "void AddItemToLabelQueue(int id, Point position)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_cells::suppressSideEffects)\n"
     "\t\treturn;\n"
     "#endif\n"),
    # ---- overlays: drawing outside the scene reports its rectangle (stage 2) ----
    ("Source/engine/render/clx_render.cpp",
     "\tif (amiga_cells::mode != amiga_cells::Mode::Off) { \\\n",
     "\tamiga_cells::RecordOverlay(out, position.x - (EXTRA), position.y - clx.height() + 1 - (EXTRA), clx.width() + 2 * (EXTRA), clx.height() + 2 * (EXTRA)); \\\n"
     "\tif (amiga_cells::mode != amiga_cells::Mode::Off) { \\\n"),
    ("Source/engine/render/primitive_render.cpp",
     "#include \"engine/surface.hpp\"\n",
     "#include \"engine/surface.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"
     "#ifdef __amigaos__\n#define AMIGA_OVERLAY(X, Y, W, H) amiga_cells::RecordOverlay(out, (X), (Y), (W), (H))\n"
     "#else\n#define AMIGA_OVERLAY(X, Y, W, H)\n#endif\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void FillRect(const Surface &out, int x, int y, int width, int height, uint8_t colorIndex)\n{\n",
     "void FillRect(const Surface &out, int x, int y, int width, int height, uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(x, y, width, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex)\n{\n",
     "void DrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, width, 1);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void UnsafeDrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex)\n{\n",
     "void UnsafeDrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, width, 1);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex)\n{\n",
     "void DrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, 1, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void UnsafeDrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex)\n{\n",
     "void UnsafeDrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, 1, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawHalfTransparentHorizontalLine(const Surface &out, Point from, int width, uint8_t colorIndex)\n{\n",
     "void DrawHalfTransparentHorizontalLine(const Surface &out, Point from, int width, uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, width, 1);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawHalfTransparentVerticalLine(const Surface &out, Point from, int height, uint8_t colorIndex)\n{\n",
     "void DrawHalfTransparentVerticalLine(const Surface &out, Point from, int height, uint8_t colorIndex)\n{\n\tAMIGA_OVERLAY(from.x, from.y, 1, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height)\n{\n",
     "void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height)\n{\n\tAMIGA_OVERLAY(sx, sy, width, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height, uint8_t color)\n{\n",
     "void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height, uint8_t color)\n{\n\tAMIGA_OVERLAY(sx, sy, width, height);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void SetHalfTransparentPixel(const Surface &out, Point position, uint8_t color)\n{\n",
     "void SetHalfTransparentPixel(const Surface &out, Point position, uint8_t color)\n{\n\tAMIGA_OVERLAY(position.x, position.y, 1, 1);\n"),
    ("Source/engine/render/primitive_render.cpp",
     "void UnsafeDrawBorder2px(const Surface &out, Rectangle rect, uint8_t color)\n{\n",
     "void UnsafeDrawBorder2px(const Surface &out, Rectangle rect, uint8_t color)\n{\n\tAMIGA_OVERLAY(rect.position.x, rect.position.y, rect.size.width, rect.size.height);\n"),
    ("Source/engine/surface.cpp",
     "#include \"engine/surface.hpp\"\n",
     "#include \"engine/surface.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/engine/surface.cpp",
     "void Surface::BlitFrom(const Surface &src, SDL_Rect srcRect, Point targetPosition) const\n{\n",
     "void Surface::BlitFrom(const Surface &src, SDL_Rect srcRect, Point targetPosition) const\n{\n"
     "#ifdef __amigaos__\n\tamiga_cells::RecordOverlay(*this, targetPosition.x, targetPosition.y, srcRect.w, srcRect.h);\n#endif\n"),
    ("Source/engine/surface.cpp",
     "void Surface::BlitFromSkipColorIndexZero(const Surface &src, SDL_Rect srcRect, Point targetPosition) const\n{\n",
     "void Surface::BlitFromSkipColorIndexZero(const Surface &src, SDL_Rect srcRect, Point targetPosition) const\n{\n"
     "#ifdef __amigaos__\n\tamiga_cells::RecordOverlay(*this, targetPosition.x, targetPosition.y, srcRect.w, srcRect.h);\n#endif\n"),
    ("Source/engine/render/scrollrt.cpp",
     "void UndrawCursor(const Surface &out)\n{\n\tDrawnCursor &cursor = GetDrawnCursor();\n",
     "void UndrawCursor(const Surface &out)\n{\n\tDrawnCursor &cursor = GetDrawnCursor();\n"
     "#ifdef __amigaos__\n"
     "\tamiga_cells::RecordOverlay(out, cursor.rect.position.x, cursor.rect.position.y, cursor.rect.size.width, cursor.rect.size.height);\n"
     "#endif\n"),
    ("Source/engine/render/scrollrt.cpp",
     "\tDrawGame(out, startPosition, offset);\n\tif (AutomapActive) {\n",
     "\tDrawGame(out, startPosition, offset);\n"
     "#ifdef __amigaos__\n"
     "\t// screens that cover or repaint the view: send all of it\n"
     "\tif (AutomapActive || IsLeftPanelOpen() || IsRightPanelOpen() || IsPlayerInStore() || qtextflag || HelpFlag\n"
     "\t    || ChatLogFlag || MyPlayerIsDead || PauseMode != 0 || gmenu_is_active() || DoomFlag)\n"
     "\t\tamiga_cells::OverlayAll();\n"
     "#endif\n"
     "\tif (AutomapActive) {\n"),
    # Path finding: O(1) node lookup (same search, same paths) - the player
    # walking towards a wall took up to 2.6 s per path on a 68060.
    ("Source/engine/path.cpp",
     '// A simple map with a fixed number of buckets and static storage.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\tusing Bucket = StaticVector<Entry, BucketCapacity>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst Bucket &b = bucket(point);\n\t\tconst auto *const it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tBucket &b = bucket(point);\n\t\tauto *it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tbucket(point).emplace_back(repr(point), exploredNode);\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn bucket(point).size() < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] const Bucket &bucket(const PointT &point) const { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] Bucket &bucket(const PointT &point) { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\tstd::array<Bucket, NumBuckets> buckets_;\n};\n\n',
     '#ifdef __amigaos__\n// Warp port: the same buckets with the same capacity (so the search explores\n// exactly the same nodes), but a node is found through a 256x256 index\n// instead of a linear search of its bucket. The comparator of the frontier\n// heap looks nodes up on every tie: on a 68060 an unreachable target (the\n// player walking towards a wall with the button held) took up to 2.6 s.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\tExploredNodes()\n\t{\n\t\tif (++stamp_ == 0) {\n\t\t\tstd::fill(std::begin(stamps_), std::end(stamps_), 0);\n\t\t\tstamp_ = 1;\n\t\t}\n\t\tcounts_.fill(0);\n\t}\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\treturn stamps_[r] == stamp_ ? &entries_[index_[r]] : nullptr;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\treturn stamps_[r] == stamp_ ? &entries_[index_[r]] : nullptr;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\tindex_[r] = count_;\n\t\tentries_[count_++] = Entry { r, exploredNode };\n\t\tstamps_[r] = stamp_;\n\t\tcounts_[bucketIndex(point)]++;\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn counts_[bucketIndex(point)] < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\t// one FindPath at a time: the index is shared, entries of an old search\n\t// are told apart by the stamp\n\tstatic inline uint16_t stamps_[65536];\n\tstatic inline uint16_t index_[65536];\n\tstatic inline uint16_t stamp_ = 0;\n\tstd::array<uint8_t, NumBuckets> counts_;\n\tstd::array<Entry, NumBuckets * BucketCapacity> entries_;\n\tuint16_t count_ = 0;\n};\n#else\n// A simple map with a fixed number of buckets and static storage.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\tusing Bucket = StaticVector<Entry, BucketCapacity>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst Bucket &b = bucket(point);\n\t\tconst auto *const it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tBucket &b = bucket(point);\n\t\tauto *it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tbucket(point).emplace_back(repr(point), exploredNode);\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn bucket(point).size() < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] const Bucket &bucket(const PointT &point) const { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] Bucket &bucket(const PointT &point) { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\tstd::array<Bucket, NumBuckets> buckets_;\n};\n\n#endif\n\n'),
    # Path finding: the heuristic of a frontier node kept in the node (it only
    # depends on the position and the target) instead of computed twice in
    # every tie of the heap comparator. Same paths (tools: build/pathtest).
    ("Source/engine/path.cpp",
     "\tCostType f;\n};\n",
     "\tCostType f;\n#ifdef __amigaos__\n\t// heuristic cost to the destination (Warp port: cached)\n\tCostType h;\n#endif\n};\n"),
    ("Source/engine/path.cpp",
     "\t\tfrontier.emplace_back(FrontierNode { .position = start, .f = initialHeuristicCost });\n",
     "#ifdef __amigaos__\n"
     "\t\tfrontier.emplace_back(FrontierNode { .position = start, .f = initialHeuristicCost, .h = initialHeuristicCost });\n"
     "#else\n"
     "\t\tfrontier.emplace_back(FrontierNode { .position = start, .f = initialHeuristicCost });\n"
     "#endif\n"),
    ("Source/engine/path.cpp",
     "\t\tconst CostType hA = GetHeuristicCost(a.position, dest);\n\t\tconst CostType hB = GetHeuristicCost(b.position, dest);\n",
     "#ifdef __amigaos__\n"
     "\t\tconst CostType hA = a.h;\n\t\tconst CostType hB = b.h;\n"
     "#else\n"
     "\t\tconst CostType hA = GetHeuristicCost(a.position, dest);\n\t\tconst CostType hB = GetHeuristicCost(b.position, dest);\n"
     "#endif\n"),
    ("Source/engine/path.cpp",
     "\t\t\t\tconst CostType f = g + GetHeuristicCost(neighborPos, dest);\n",
     "\t\t\t\tconst CostType h = GetHeuristicCost(neighborPos, dest);\n\t\t\t\tconst CostType f = g + h;\n"),
    ("Source/engine/path.cpp",
     "\t\t\t\t\tfrontier.emplace_back(FrontierNode { .position = neighborPos, .f = f });\n",
     "#ifdef __amigaos__\n"
     "\t\t\t\t\tfrontier.emplace_back(FrontierNode { .position = neighborPos, .f = f, .h = h });\n"
     "#else\n"
     "\t\t\t\t\tfrontier.emplace_back(FrontierNode { .position = neighborPos, .f = f });\n"
     "#endif\n"),
    # Test mode ENV:DIABLO_GOD: the own player takes no damage (measurements
    # in the dungeon without dying). Off unless the variable is set.
    ("Source/player.cpp",
     "void ApplyPlrDamage(DamageType damageType, Player &player, Fixed26_6 dam, int minHP /*= 0*/, DeathReason deathReason /*= DeathReason::MonsterOrTrap*/)\n{\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" int msdl_test_god(void);\n"
     "#endif\n"
     "void ApplyPlrDamage(DamageType damageType, Player &player, Fixed26_6 dam, int minHP /*= 0*/, DeathReason deathReason /*= DeathReason::MonsterOrTrap*/)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (&player == MyPlayer && msdl_test_god() != 0)\n"
     "\t\treturn;\n"
     "#endif\n"),
    # No colour hardware cursor: the game draws its own.
    ("Source/options.cpp",
     "#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE == 1) || __DJGPP__\n\treturn false;",
     "#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE == 1) || __DJGPP__ || defined(__amigaos__)\n\treturn false;"),
]


def apply(tree, upstream):
    """Every changed file is made again from the clean upstream copy and
    written only when the result differs (no needless rebuilds)."""
    here = os.path.dirname(os.path.abspath(__file__))
    results = {}
    for rel, old, new in CHANGES:
        if rel not in results:
            with open(os.path.join(upstream, rel), "rb") as f:
                results[rel] = f.read().decode("utf-8")
        data = results[rel]
        crlf = "\r\n" in data
        o = old.replace("\n", "\r\n") if crlf else old
        n = new.replace("\n", "\r\n") if crlf else new
        count = data.count(o)
        if count != 1:
            sys.exit("apply_port: %s: expected text found %d times:\n%s" % (rel, count, old))
        results[rel] = data.replace(o, n)
    for rel, data in results.items():
        path = os.path.join(tree, rel)
        out = data.encode("utf-8")
        if os.path.exists(path) and open(path, "rb").read() == out:
            continue
        with open(path, "wb") as f:
            f.write(out)
        print("changed", rel)
    overlay = os.path.join(here, "overlay")
    for root, _dirs, files in os.walk(overlay):
        for name in files:
            src = os.path.join(root, name)
            rel = os.path.relpath(src, overlay)
            dst = os.path.join(tree, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.exists(dst) and open(dst, "rb").read() == open(src, "rb").read():
                continue        # unchanged: keep the timestamp (no rebuild)
            shutil.copyfile(src, dst)
            print("overlay", rel)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    apply(sys.argv[1], sys.argv[2])
