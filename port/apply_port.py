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
    # An event with no handler: when the game starts, the loading screen
    # restores the handler it found (none yet) and events can still be
    # dispatched before GameEventHandler is set; upstream only asserts, a
    # release build jumps to address 0 (crash with F10 / keys pressed at the
    # end of loading, 4.11.2026). Here the event is dropped and logged.
    ("Source/engine/events.cpp",
     "\tassert(CurrentEventHandler != nullptr);\n\n\tCurrentEventHandler(event, modState);\n",
     "#ifdef __amigaos__\n"
     "\tif (CurrentEventHandler == nullptr) {\n"
     "\t\tLogError(\"HandleMessage: no event handler, event {:#x} dropped\", static_cast<uint32_t>(event.type));\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"
     "\tassert(CurrentEventHandler != nullptr);\n\n\tCurrentEventHandler(event, modState);\n"),
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
     "extern \"C\" void (*amiga_blit_map_fn)(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map);\n"
     "extern \"C\" void (*amiga_blit_blend_map_fn)(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map, const unsigned char *pal);\n"
     "extern \"C\" void (*amiga_map_lines_fn)(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count, const unsigned char *map, const unsigned char *pal);\n"
     "extern \"C\" void (*amiga_blend_lines_fn)(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count, const unsigned char *map, const unsigned char *pal);\n"
     "#endif\n"
     "\n"
     "namespace devilution {\n"),
    # Whole unclipped tiles (square, triangle halves) with a light table in one
    # call each instead of one call per line (16-32 calls of 2-32 pixels).
    ("Source/engine/render/dun_render.cpp",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, const Lightmap &lightmap)\n{\n",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderSquareFull(uint8_t *DVL_RESTRICT dst, uint16_t dstPitch, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, const Lightmap &lightmap)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif constexpr (Light == LightType::PartiallyLit) {\n"
     "\t\t(Transparent ? amiga_blend_lines_fn : amiga_map_lines_fn)(dst, dstPitch, src, Width, 0, Height, tbl, &paletteTransparencyLookup[0][0]);\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/engine/render/dun_render.cpp",
     "\t} else { // Mask == MaskType::Solid || Mask == MaskType::Transparent\n",
     "\t} else { // Mask == MaskType::Solid || Mask == MaskType::Transparent\n"
     "#ifdef __amigaos__\n"
     "\t\tif constexpr (Light == LightType::PartiallyLit) {\n"
     "\t\t\t(Mask == MaskType::Transparent ? amiga_blend_lines_fn : amiga_map_lines_fn)(dst, dstPitch, src, Width, 0, TrapezoidUpperHeight, tbl, &paletteTransparencyLookup[0][0]);\n"
     "\t\t\treturn;\n"
     "\t\t}\n"
     "#endif\n"),
    ("Source/engine/render/dun_render.cpp",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTriangleLower(uint8_t *DVL_RESTRICT &dst, ptrdiff_t dstLineOffset, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl, const Lightmap *lightmap)\n{\n",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTriangleLower(uint8_t *DVL_RESTRICT &dst, ptrdiff_t dstLineOffset, const uint8_t *DVL_RESTRICT &src, const uint8_t *DVL_RESTRICT tbl, const Lightmap *lightmap)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif constexpr (Light == LightType::PartiallyLit) {\n"
     "\t\t(Transparent ? amiga_blend_lines_fn : amiga_map_lines_fn)(dst, static_cast<int>(dstLineOffset), src, 2, 2, 16, tbl, &paletteTransparencyLookup[0][0]);\n"
     "\t\tsrc += 272;\n"
     "\t\tdst -= 16 * dstLineOffset;\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/engine/render/dun_render.cpp",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTriangleUpper(uint8_t *DVL_RESTRICT dst, ptrdiff_t dstLineOffset, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, const Lightmap *lightmap)\n{\n",
     "DVL_ALWAYS_INLINE DVL_ATTRIBUTE_HOT void RenderTriangleUpper(uint8_t *DVL_RESTRICT dst, ptrdiff_t dstLineOffset, const uint8_t *DVL_RESTRICT src, const uint8_t *DVL_RESTRICT tbl, const Lightmap *lightmap)\n{\n"
     "#ifdef __amigaos__\n"
     "\tif constexpr (Light == LightType::PartiallyLit) {\n"
     "\t\t(Transparent ? amiga_blend_lines_fn : amiga_map_lines_fn)(dst, static_cast<int>(dstLineOffset), src, 30, -2, 15, tbl, &paletteTransparencyLookup[0][0]);\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/engine/render/blit_impl.hpp",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n"
     "#ifdef __amigaos__\n"
     "\tamiga_blit_map_fn(dst, src, length, colorMap);\n"
     "\treturn;\n"
     "#endif\n"),
    ("Source/engine/render/blit_impl.hpp",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsBlendedWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n",
     "inline DVL_NO_INLINE DVL_ATTRIBUTE_HOT void BlitPixelsBlendedWithMap(uint8_t *DVL_RESTRICT dst, const uint8_t *DVL_RESTRICT src, unsigned length, const uint8_t *DVL_RESTRICT colorMap)\n{\n\tDVL_ASSUME(length != 0);\n"
     "#ifdef __amigaos__\n"
     "\tamiga_blit_blend_map_fn(dst, src, length, colorMap, &paletteTransparencyLookup[0][0]);\n"
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
     "\tif (amiga_cells::mode == amiga_cells::Mode::Record) {\n"
     "\t\tamiga_cells::RecordAt(out, position.x, position.y - height + 1, DunFrameWidth, height,\n"
     "\t\t    amiga_cells::PtrKey(src), (static_cast<uint32_t>(tile) << 8) | static_cast<uint32_t>(maskType), amiga_cells::PtrKey(tbl),\n"
     "\t\t    maskType == MaskType::Left || maskType == MaskType::Right);\n"
     "\t\treturn;\n"
     "\t}\n"
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
     "#include \"engine/render/scrollrt.h\"\n#include \"engine/render/amiga_cells.hpp\"\n"
     "#ifdef __amigaos__\n#define AMIGA_TILE() amiga_cells::BeginTile()\n#else\n#define AMIGA_TILE() true\n#endif\n"),
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
    # automap (opaque, the default): kept on the game's buffer, only changes
    # sent (amiga_cells.hpp AutomapBegin); other map types as before
    ("Source/engine/render/scrollrt.cpp",
     "\tDrawGame(out, startPosition, offset);\n\tif (AutomapActive) {\n\t\tDrawAutomap(out.subregionY(0, gnViewportHeight));\n\t}\n",
     "#ifdef __amigaos__\n"
     "\tconst bool amigaMapKept = AutomapActive && GetAutomapType() == AutomapType::Opaque && !*GetOptions().Graphics.zoom;\n"
     "\tamiga_cells::AutomapBegin(amigaMapKept);\n"
     "#endif\n"
     "\tDrawGame(out, startPosition, offset);\n"
     "#ifdef __amigaos__\n"
     "\t// screens that cover or repaint the view: send all of it\n"
     "\tif (IsLeftPanelOpen() || IsRightPanelOpen() || IsPlayerInStore() || qtextflag || HelpFlag\n"
     "\t    || ChatLogFlag || MyPlayerIsDead || PauseMode != 0 || gmenu_is_active() || DoomFlag)\n"
     "\t\tamiga_cells::OverlayAll();\n"
     "#endif\n"
     "\tif (AutomapActive) {\n"
     "#ifdef __amigaos__\n"
     "\t\tamiga_cells::AutomapDrawStart();\n"
     "\t\tDrawAutomap(out.subregionY(0, gnViewportHeight));\n"
     "\t\tif (amiga_cells::AutomapEnd(out.subregionY(0, gnViewportHeight))) {\n"
     "\t\t\tamiga_cells::AutomapDrawStart();\n"
     "\t\t\tDrawAutomap(out.subregionY(0, gnViewportHeight));\n"
     "\t\t}\n"
     "#else\n"
     "\t\tDrawAutomap(out.subregionY(0, gnViewportHeight));\n"
     "#endif\n"
     "\t}\n"),
    # automap tiles: pixels captured once per tile and replayed (amiga_cells.hpp MapTile)
    ("Source/automap.cpp",
     "#include \"engine/render/automap_render.hpp\"\n",
     "#include \"engine/render/automap_render.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/automap.cpp",
     "Displacement GetAutomapScreen()\n{\n",
     "#ifdef __amigaos__\n"
     "void AmigaAutomapTile(const Surface &out, Point center, Point map)\n"
     "{\n"
     "\tif (GetAutomapType() != AutomapType::Opaque) {\n"
     "\t\tDrawAutomapTile(out, center, map);\n"
     "\t\treturn;\n"
     "\t}\n"
     "\tconst int how = amiga_cells::MapTile(out, center.x, center.y, map.x, map.y);\n"
     "\tif (how == 0)\n"
     "\t\treturn;\n"
     "\tDrawAutomapTile(out, center, map);\n"
     "\tif (how == 1)\n"
     "\t\tamiga_cells::MapTileEnd();\n"
     "}\n"
     "#endif\n"
     "\n"
     "Displacement GetAutomapScreen()\n{\n"),
    ("Source/automap.cpp",
     "\tPoint map = { Automap.x - cells, Automap.y - 1 };\n",
     "\tPoint map = { Automap.x - cells, Automap.y - 1 };\n"
     "#ifdef __amigaos__\n"
     "\tstatic_assert(DMAXX == 40 && DMAXY == 40);\n"
     "\tamiga_cells::MapTilesBegin(static_cast<uint32_t>(currlevel) ^ (static_cast<uint32_t>(setlvlnum) << 8) ^ (static_cast<uint32_t>(leveltype) << 16)\n"
     "\t        ^ (setlevel ? 0x1000000U : 0U) ^ (static_cast<uint32_t>(GetAutomapType()) << 26) ^ (static_cast<uint32_t>(Quests[Q_PWATER]._qactive) << 28)\n"
     "\t        ^ (static_cast<uint32_t>(scale) * 2654435761U),\n"
     "\t    &AutomapView[0][0], &dungeon[0][0]);\n"
     "\tconst bool amigaFrameDone = amiga_cells::MapFrameBegin(out, screen.x, screen.y, map.x, map.y, cells, GetAutomapType() == AutomapType::Opaque);\n"
     "#define DrawAutomapTile AmigaAutomapTile\n"
     "#endif\n"),
    ("Source/automap.cpp",
     "\tfor (int i = 0; i <= cells + 1; i++) {\n",
     "#ifdef __amigaos__\n\tfor (int i = 0; !amigaFrameDone && i <= cells + 1; i++) {\n#else\n"
     "\tfor (int i = 0; i <= cells + 1; i++) {\n#endif\n"),
    ("Source/automap.cpp",
     "\t\tscreen.y += AmOffset(AmWidthOffset::None, AmHeightOffset::DoubleTileDown).deltaY;\n\t}\n",
     "\t\tscreen.y += AmOffset(AmWidthOffset::None, AmHeightOffset::DoubleTileDown).deltaY;\n\t}\n"
     "#ifdef __amigaos__\n\tamiga_cells::MapPlayersBegin();\n#endif\n"),
    ("Source/automap.cpp",
     "\tDrawAutomapText(out);\n}\n",
     "#ifdef __amigaos__\n#undef DrawAutomapTile\n\tamiga_cells::MapTextBegin();\n"
     "\t// the text depends on these only (single player): cached as its pixels\n"
     "\tif (gbIsMultiplayer || !amiga_cells::MapTextCached(out,\n"
     "\t        static_cast<uint32_t>(currlevel) ^ (static_cast<uint32_t>(setlvlnum) << 8) ^ (static_cast<uint32_t>(leveltype) << 16)\n"
     "\t            ^ (setlevel ? 0x1000000U : 0U) ^ (*GetOptions().Graphics.showFPS ? 0x2000000U : 0U)\n"
     "\t            ^ (static_cast<uint32_t>(sgGameInitInfo.nDifficulty) << 28),\n"
     "\t        [](const Surface &s) { DrawAutomapText(s); }))\n"
     "\t\tDrawAutomapText(out);\n"
     "#else\n"
     "\tDrawAutomapText(out);\n"
     "#endif\n"
     "}\n"),
    ("Source/engine/render/automap_render.cpp",
     "#include \"engine/render/automap_render.hpp\"\n",
     "#include \"engine/render/automap_render.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
    ("Source/engine/render/automap_render.cpp",
     "\tif (GetAutomapType() == AutomapType::Transparent) {\n\t\tSetHalfTransparentPixel(out, position, color);\n\t} else {\n\t\tout.SetPixel(position, color);\n\t}\n",
     "\tif (GetAutomapType() == AutomapType::Transparent) {\n\t\tSetHalfTransparentPixel(out, position, color);\n\t} else {\n"
     "#ifdef __amigaos__\n\t\tamiga_cells::MapPixel(position.x, position.y, color);\n#endif\n"
     "\t\tout.SetPixel(position, color);\n\t}\n"),
    # Path finding: O(1) node lookup (same search, same paths) - the player
    # walking towards a wall took up to 2.6 s per path on a 68060.
    ("Source/engine/path.cpp",
     '// A simple map with a fixed number of buckets and static storage.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\tusing Bucket = StaticVector<Entry, BucketCapacity>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst Bucket &b = bucket(point);\n\t\tconst auto *const it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tBucket &b = bucket(point);\n\t\tauto *it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tbucket(point).emplace_back(repr(point), exploredNode);\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn bucket(point).size() < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] const Bucket &bucket(const PointT &point) const { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] Bucket &bucket(const PointT &point) { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\tstd::array<Bucket, NumBuckets> buckets_;\n};\n\n',
     '#ifdef __amigaos__\n// Warp port: the same buckets with the same capacity (so the search explores\n// exactly the same nodes), but a node is found through a 256x256 index\n// instead of a linear search of its bucket. The comparator of the frontier\n// heap looks nodes up on every tie: on a 68060 an unreachable target (the\n// player walking towards a wall with the button held) took up to 2.6 s.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\tExploredNodes()\n\t{\n\t\tif (++stamp_ == 0) {\n\t\t\tstd::fill(std::begin(stamps_), std::end(stamps_), 0);\n\t\t\tstamp_ = 1;\n\t\t}\n\t\tcounts_.fill(0);\n\t}\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\treturn stamps_[r] == stamp_ ? &entries_[index_[r]] : nullptr;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\treturn stamps_[r] == stamp_ ? &entries_[index_[r]] : nullptr;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tconst uint16_t r = repr(point);\n\t\tindex_[r] = count_;\n\t\tentries_[count_++] = Entry { r, exploredNode };\n\t\tstamps_[r] = stamp_;\n\t\tcounts_[bucketIndex(point)]++;\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn counts_[bucketIndex(point)] < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\t// one FindPath at a time: the index is shared, entries of an old search\n\t// are told apart by the stamp\n\tstatic inline uint16_t stamps_[65536];\n\tstatic inline uint16_t index_[65536];\n\tstatic inline uint16_t stamp_ = 0;\n\tstd::array<uint8_t, NumBuckets> counts_;\n\t// not zeroed on every FindPath (std::array of pairs would be: 18 KB per call)\n\tunion Storage {\n\t\tStorage() { }\n\t\tEntry e[NumBuckets * BucketCapacity];\n\t} storage_;\n\tEntry *const entries_ = storage_.e;\n\tuint16_t count_ = 0;\n};\n#else\n// A simple map with a fixed number of buckets and static storage.\nclass ExploredNodes {\n\tstatic const size_t NumBuckets = 64;\n\tstatic const size_t BucketCapacity = 3 * MaxPathNodes / NumBuckets;\n\tusing Entry = std::pair<uint16_t, ExploredNode>;\n\tusing Bucket = StaticVector<Entry, BucketCapacity>;\n\npublic:\n\tusing value_type = Entry;\n\tusing iterator = value_type *;\n\tusing const_iterator = const value_type *;\n\n\t[[nodiscard]] const_iterator find(const PointT &point) const\n\t{\n\t\tconst Bucket &b = bucket(point);\n\t\tconst auto *const it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\t[[nodiscard]] iterator find(const PointT &point)\n\t{\n\t\tBucket &b = bucket(point);\n\t\tauto *it = c_find_if(b, [r = repr(point)](const Entry &e) { return e.first == r; });\n\t\tif (it == b.end()) return nullptr;\n\t\treturn it;\n\t}\n\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] const_iterator end() const { return nullptr; }\n\t// NOLINTNEXTLINE(readability-convert-member-functions-to-static)\n\t[[nodiscard]] iterator end() { return nullptr; }\n\n\tvoid emplace(const PointT &point, const ExploredNode &exploredNode)\n\t{\n\t\tbucket(point).emplace_back(repr(point), exploredNode);\n\t}\n\n\t[[nodiscard]] bool canInsert(const PointT &point) const\n\t{\n\t\treturn bucket(point).size() < BucketCapacity;\n\t}\n\nprivate:\n\t[[nodiscard]] const Bucket &bucket(const PointT &point) const { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] Bucket &bucket(const PointT &point) { return buckets_[bucketIndex(point)]; }\n\t[[nodiscard]] static size_t bucketIndex(const PointT &point)\n\t{\n\t\treturn ((point.x & 0b111) << 3) | (point.y & 0b111);\n\t}\n\n\t[[nodiscard]] static uint16_t repr(const PointT &point)\n\t{\n\t\treturn (point.x << 8) | point.y;\n\t}\n\n\tstd::array<Bucket, NumBuckets> buckets_;\n};\n\n#endif\n\n'),
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
    # tile visits: the render pass skips the ones that cannot touch a dirty rectangle
    ("Source/engine/render/scrollrt.cpp",
     "\t\t\t\tDrawFloorTile(out, lightmap, tilePosition, targetBufferPosition);\n",
     "\t\t\t\tif (AMIGA_TILE())\n\t\t\t\t\tDrawFloorTile(out, lightmap, tilePosition, targetBufferPosition);\n"),
    ("Source/engine/render/scrollrt.cpp",
     "\t\t\t\t\t\t\tDrawDungeon(out, lightmap, tilePosition + Direction::East, { targetBufferPosition.x + TILE_WIDTH, targetBufferPosition.y });\n",
     "\t\t\t\t\t\t\tif (AMIGA_TILE())\n\t\t\t\t\t\t\t\tDrawDungeon(out, lightmap, tilePosition + Direction::East, { targetBufferPosition.x + TILE_WIDTH, targetBufferPosition.y });\n"),
    ("Source/engine/render/scrollrt.cpp",
     "\t\t\t\t\tDrawDungeon(out, lightmap, tilePosition, targetBufferPosition);\n",
     "\t\t\t\t\tif (AMIGA_TILE())\n\t\t\t\t\t\tDrawDungeon(out, lightmap, tilePosition, targetBufferPosition);\n"),
    ("Source/engine/render/scrollrt.cpp",
     "\t\t\t\tif (leveltype == DTYPE_TOWN) {\n\t\t\t\t\tworld_draw_black_tile(out, targetBufferPosition.x, targetBufferPosition.y);\n\t\t\t\t} else {\n\t\t\t\t\tDrawDirtTile(out, lightmap, tilePosition, targetBufferPosition);\n\t\t\t\t}\n",
     "\t\t\t\tif (!AMIGA_TILE()) {\n\t\t\t\t} else if (leveltype == DTYPE_TOWN) {\n\t\t\t\t\tworld_draw_black_tile(out, targetBufferPosition.x, targetBufferPosition.y);\n\t\t\t\t} else {\n\t\t\t\t\tDrawDirtTile(out, lightmap, tilePosition, targetBufferPosition);\n\t\t\t\t}\n"),
    # Path finding: posOk of a tile is asked from up to 8 neighbours; its
    # answer does not change during one search, so it is remembered (stamped
    # 256x256 table). Same paths (tools/pathtest.sh).
    ("Source/engine/path.cpp",
     "\t\t\tconst bool ok = posOk(neighborPos);\n",
     "#ifdef __amigaos__\n"
     "\t\t\tbool ok;\n"
     "\t\t\t{\n"
     "\t\t\t\tconst uint16_t r = static_cast<uint16_t>((neighborPos.x << 8) | neighborPos.y);\n"
     "\t\t\t\tif (posOkStamp[r] == posOkCall) {\n"
     "\t\t\t\t\tok = posOkValue[r] != 0;\n"
     "\t\t\t\t} else {\n"
     "\t\t\t\t\tok = posOk(neighborPos);\n"
     "\t\t\t\t\tposOkStamp[r] = posOkCall;\n"
     "\t\t\t\t\tposOkValue[r] = ok ? 1 : 0;\n"
     "\t\t\t\t}\n"
     "\t\t\t}\n"
     "#else\n"
     "\t\t\tconst bool ok = posOk(neighborPos);\n"
     "#endif\n"),
    ("Source/engine/path.cpp",
     "int FindPath(tl::function_ref<bool(Point, Point)> canStep, tl::function_ref<bool(Point)> posOk, Point startPosition, Point destinationPosition, int8_t *path, size_t maxPathLength)\n{\n",
     "#ifdef __amigaos__\n"
     "namespace {\n"
     "uint16_t posOkStamp[65536];\n"
     "uint8_t posOkValue[65536];\n"
     "uint16_t posOkCall = 0;\n"
     "} // namespace\n"
     "extern \"C\" unsigned long msdl_counter[10];\n"
     "extern \"C\" unsigned long amiga_eclock(void);\n"
     "struct AmigaPathTimer {\n\tunsigned long t0 = amiga_eclock();\n\t~AmigaPathTimer() { msdl_counter[2] += amiga_eclock() - t0; }\n};\n"
     "namespace {\n"
     "// Warp port: can the search reach the destination at all? Breadth-first\n"
     "// over the same steps (posOk, canStep, a blocked destination allowed) and\n"
     "// at least as deep as the A* can go (a node is expanded there only with\n"
     "// g < 101 * maxPathLength, and g >= 100 * steps). Everything the A* can\n"
     "// reach is found here, so 'not reached' means the A* returns 0 as well;\n"
     "// otherwise the A* simply goes on (the same result either way).\n"
     "uint16_t floodStamp[65536];\n"
     "uint16_t floodCall = 0;\n"
     "PointOf<uint8_t> floodQueue[65536];\n"
     "uint8_t floodDepth[65536];\n"
     "bool AmigaReachable(tl::function_ref<bool(Point, Point)> canStep, tl::function_ref<bool(Point)> posOk, PointOf<uint8_t> start, PointOf<uint8_t> dest, size_t maxPathLength)\n"
     "{\n"
     "\tif (++floodCall == 0) {\n"
     "\t\tstd::fill(std::begin(floodStamp), std::end(floodStamp), 0);\n"
     "\t\tfloodCall = 1;\n"
     "\t}\n"
     "\tunsigned head = 0, tail = 0;\n"
     "\tfloodQueue[tail] = start;\n"
     "\tfloodDepth[tail++] = 0;\n"
     "\tfloodStamp[(start.x << 8) | start.y] = floodCall;\n"
     "\twhile (head < tail) {\n"
     "\t\tconst PointOf<uint8_t> cur = floodQueue[head];\n"
     "\t\tconst unsigned depth = floodDepth[head++];\n"
     "\t\tif (100 * depth >= 101 * maxPathLength) continue;\n"
     "\t\tfor (const DisplacementOf<int8_t> d : PathDirs) {\n"
     "\t\t\tif ((cur.x == 0 && d.deltaX < 0) || (cur.y == 0 && d.deltaY < 0)) continue;\n"
     "\t\t\tconst PointOf<uint8_t> n = cur + d;\n"
     "\t\t\tconst uint16_t r = static_cast<uint16_t>((n.x << 8) | n.y);\n"
     "\t\t\tif (floodStamp[r] == floodCall) continue;\n"
     "\t\t\tbool ok;\n"
     "\t\t\tif (posOkStamp[r] == posOkCall) {\n"
     "\t\t\t\tok = posOkValue[r] != 0;\n"
     "\t\t\t} else {\n"
     "\t\t\t\tok = posOk(n);\n"
     "\t\t\t\tposOkStamp[r] = posOkCall;\n"
     "\t\t\t\tposOkValue[r] = ok ? 1 : 0;\n"
     "\t\t\t}\n"
     "\t\t\tif (n == dest) {\n"
     "\t\t\t\tif (!ok || canStep(cur, n)) return true;\n"
     "\t\t\t\tcontinue;\n"
     "\t\t\t}\n"
     "\t\t\tif (!ok || !canStep(cur, n)) continue;\n"
     "\t\t\tfloodStamp[r] = floodCall;\n"
     "\t\t\tif (tail >= 65536) return true;\n"
     "\t\t\tfloodQueue[tail] = n;\n"
     "\t\t\tfloodDepth[tail++] = static_cast<uint8_t>(depth + 1 > 255 ? 255 : depth + 1);\n"
     "\t\t}\n"
     "\t}\n"
     "\treturn false;\n"
     "}\n"
     "// Backwards from the destination (an enclosed one has a small region):\n"
     "// 0 = no path of any length, 1 = the start can reach it, 2 = undecided\n"
     "// within the budget. Edge u -> v is the A*'s step: u is the start or posOk,\n"
     "// v is posOk with canStep(u, v), or v is a blocked destination.\n"
     "int AmigaReachBack(tl::function_ref<bool(Point, Point)> canStep, tl::function_ref<bool(Point)> posOk, PointOf<uint8_t> start, PointOf<uint8_t> dest, unsigned budget)\n"
     "{\n"
     "\tif (++floodCall == 0) {\n"
     "\t\tstd::fill(std::begin(floodStamp), std::end(floodStamp), 0);\n"
     "\t\tfloodCall = 1;\n"
     "\t}\n"
     "\tconst auto okAt = [&](PointOf<uint8_t> p) {\n"
     "\t\tconst uint16_t r = static_cast<uint16_t>((p.x << 8) | p.y);\n"
     "\t\tif (posOkStamp[r] != posOkCall) {\n"
     "\t\t\tposOkStamp[r] = posOkCall;\n"
     "\t\t\tposOkValue[r] = posOk(p) ? 1 : 0;\n"
     "\t\t}\n"
     "\t\treturn posOkValue[r] != 0;\n"
     "\t};\n"
     "\tunsigned head = 0, tail = 0;\n"
     "\tfloodQueue[tail++] = dest;\n"
     "\tfloodStamp[(dest.x << 8) | dest.y] = floodCall;\n"
     "\twhile (head < tail) {\n"
     "\t\tconst PointOf<uint8_t> v = floodQueue[head++];\n"
     "\t\tconst bool vOk = okAt(v);\n"
     "\t\tif (!vOk && v != dest) continue;\n"
     "\t\tfor (const DisplacementOf<int8_t> d : PathDirs) {\n"
     "\t\t\t// u = v - d, reached from u with the step d (the A*'s underflow rule)\n"
     "\t\t\tif ((v.x == 0 && d.deltaX > 0) || (v.y == 0 && d.deltaY > 0)) continue;\n"
     "\t\t\tif ((v.x == 255 && d.deltaX < 0) || (v.y == 255 && d.deltaY < 0)) continue;\n"
     "\t\t\tconst PointOf<uint8_t> u { static_cast<uint8_t>(v.x - d.deltaX), static_cast<uint8_t>(v.y - d.deltaY) };\n"
     "\t\t\tif (u == dest) continue;\n"
     "\t\t\tconst uint16_t r = static_cast<uint16_t>((u.x << 8) | u.y);\n"
     "\t\t\tif (floodStamp[r] == floodCall) continue;\n"
     "\t\t\tif (vOk && !canStep(u, v)) continue;\n"
     "\t\t\tif (u == start) return 1;\n"
     "\t\t\tif (!okAt(u)) continue;\n"
     "\t\t\tfloodStamp[r] = floodCall;\n"
     "\t\t\tif (tail >= budget) return 2;\n"
     "\t\t\tfloodQueue[tail++] = u;\n"
     "\t\t}\n"
     "\t}\n"
     "\treturn 0;\n"
     "}\n"
     "} // namespace\n"
     "#endif\n"
     "int FindPath(tl::function_ref<bool(Point, Point)> canStep, tl::function_ref<bool(Point)> posOk, Point startPosition, Point destinationPosition, int8_t *path, size_t maxPathLength)\n{\n"
     "#ifdef __amigaos__\n"
     "\tAmigaPathTimer amigaPathTimer;\n"
     "\tmsdl_counter[0]++;\n"
     "\tunsigned amigaPopped = 0;\n"
     "\tif (++posOkCall == 0) {\n"
     "\t\tstd::fill(std::begin(posOkStamp), std::end(posOkStamp), 0);\n"
     "\t\tposOkCall = 1;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/engine/path.cpp",
     "\t\tstd::pop_heap(frontier.begin(), frontier.end(), frontierComparator);\n",
     "\t\tstd::pop_heap(frontier.begin(), frontier.end(), frontierComparator);\n#ifdef __amigaos__\n\t\tmsdl_counter[1]++;\n"
     "\t\tif (++amigaPopped == 128) {\n"
     "\t\t\tconst int back = AmigaReachBack(canStep, posOk, start, dest, 512);\n"
     "\t\t\tif (back == 0 || !AmigaReachable(canStep, posOk, start, dest, maxPathLength)) {\n"
     "\t\t\t\tmsdl_counter[3]++;\n"
     "\t\t\t\treturn 0;\n"
     "\t\t\t}\n"
     "\t\t}\n"
     "#endif\n"),
    # ---- sound on Paula (port/sdl/src/amiga_sound.c): the build keeps
    # NOSOUND (no SDL_audiolib, no video sound), but the sound library is the
    # game's effects.cpp with our engine/sound_amiga.cpp and
    # utils/soundsample_amiga.cpp (overlay) instead of the stubs ----
    ("Source/CMakeLists.txt",
     "  add_devilutionx_object_library(libdevilutionx_sound\n    effects_stubs.cpp\n    engine/sound_stubs.cpp\n  )\n",
     "  add_devilutionx_object_library(libdevilutionx_sound\n    effects.cpp\n    engine/sound_amiga.cpp\n    utils/soundsample_amiga.cpp\n  )\n"),
    ("Source/engine/sound.h",
     "#ifndef NOSOUND\n#ifdef USE_SDL3\nstruct MIX_Mixer;\n#endif\n",
     "#if !defined(NOSOUND) || defined(__amigaos__)\n#ifdef USE_SDL3\nstruct MIX_Mixer;\n#endif\n"),
    ("Source/engine/sound.h",
     "#ifndef NOSOUND\n\tSoundSample DSB;\n",
     "#if !defined(NOSOUND) || defined(__amigaos__)\n\tSoundSample DSB;\n"),
    ("Source/minitext.cpp",
     "#ifndef NOSOUND\n\tuint32_t sfxFrames = GetSFXLength(nSFX);\n",
     "#if !defined(NOSOUND) || defined(__amigaos__)\n\tuint32_t sfxFrames = GetSFXLength(nSFX);\n"),
    ("Source/utils/soundsample.h",
     "#else\n\t\treturn stream_ != nullptr;\n#endif\n",
     "#elif defined(__amigaos__)\n\t\treturn pcm_ != nullptr || !file_path_.empty(); // streamed: only its path is kept\n#else\n\t\treturn stream_ != nullptr;\n#endif\n"),
    ("Source/utils/soundsample.h",
     "#else\n\tstd::unique_ptr<Aulib::Stream> stream_;\n#endif\n",
     "#elif defined(__amigaos__)\n"
     "\t// Warp port: signed 8-bit mono 22050 Hz samples for the Paula mixer\n"
     "\tArraySharedPtr<std::uint8_t> pcm_;\n"
     "\tlong pcmLen_ = 0;\n"
     "\tint voice_ = 0;\n"
     "\tint volume_ = 64;\n"
     "\tint gainL_ = 256, gainR_ = 256;\n"
     "\tbool muted_ = false;\n"
     "\tvoid VoiceVolumes(int &vl, int &vr) const;\n"
     "\n"
     "public:\n"
     "\t/** Warp port: plays once more on a voice of its own (the sound may already be playing). */\n"
     "\tbool PlayNew(int logSoundVolume, int logUserVolume, int logPan);\n"
     "#else\n\tstd::unique_ptr<Aulib::Stream> stream_;\n#endif\n"),
    # Record pass, column cache (amiga_cells.hpp): DrawCell's primitives
    # depend on the tile, its piece, light level and wall transparency only;
    # with the same key the summary of the last frame is recorded instead.
    ("Source/engine/render/scrollrt.cpp",
     "void DrawCell(const Surface &out, const Lightmap lightmap, Point tilePosition, Point targetBufferPosition, int lightTableIndex)\n{\n\tconst uint16_t levelPieceId = dPiece[tilePosition.x][tilePosition.y];\n",
     "void DrawCellImpl(const Surface &out, const Lightmap lightmap, Point tilePosition, Point targetBufferPosition, int lightTableIndex);\n"
     "\n"
     "void DrawCell(const Surface &out, const Lightmap lightmap, Point tilePosition, Point targetBufferPosition, int lightTableIndex)\n"
     "{\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_cells::mode == amiga_cells::Mode::Record && !*GetOptions().Graphics.perPixelLighting) {\n"
     "\t\tconst bool transparency = TileHasAny(tilePosition, TileProperties::Transparent) && TransList[dTransVal[tilePosition.x][tilePosition.y]];\n"
     "\t\tconst uint32_t key = (static_cast<uint32_t>(dPiece[tilePosition.x][tilePosition.y]) << 16)\n"
     "\t\t    | (static_cast<uint32_t>(lightTableIndex & 0x7FFF) << 1) | (transparency ? 1U : 0U);\n"
     "\t\tif (amiga_cells::ColumnReplay(0, out, tilePosition.x, tilePosition.y, key, targetBufferPosition.x, targetBufferPosition.y))\n"
     "\t\t\treturn;\n"
     "\t\tamiga_cells::ColumnCaptureBegin(out, targetBufferPosition.x, targetBufferPosition.y);\n"
     "\t\tDrawCellImpl(out, lightmap, tilePosition, targetBufferPosition, lightTableIndex);\n"
     "\t\tamiga_cells::ColumnCaptureEnd(0, tilePosition.x, tilePosition.y, key);\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"
     "\tDrawCellImpl(out, lightmap, tilePosition, targetBufferPosition, lightTableIndex);\n"
     "}\n"
     "\n"
     "void DrawCellImpl(const Surface &out, const Lightmap lightmap, Point tilePosition, Point targetBufferPosition, int lightTableIndex)\n{\n\tconst uint16_t levelPieceId = dPiece[tilePosition.x][tilePosition.y];\n"),
    ("Source/engine/render/scrollrt.cpp",
     "void DrawFloorTile(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition)\n{\n\tconst int lightTableIndex = dLight[tilePosition.x][tilePosition.y];\n",
     "void DrawFloorTileImpl(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition);\n"
     "\n"
     "void DrawFloorTile(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition)\n"
     "{\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_cells::mode == amiga_cells::Mode::Record && !*GetOptions().Graphics.perPixelLighting) {\n"
     "\t\tconst uint32_t key = (static_cast<uint32_t>(dPiece[tilePosition.x][tilePosition.y]) << 16)\n"
     "\t\t    | static_cast<uint32_t>(dLight[tilePosition.x][tilePosition.y] & 0xFFFF);\n"
     "\t\tif (amiga_cells::ColumnReplay(1, out, tilePosition.x, tilePosition.y, key, targetBufferPosition.x, targetBufferPosition.y))\n"
     "\t\t\treturn;\n"
     "\t\tamiga_cells::ColumnCaptureBegin(out, targetBufferPosition.x, targetBufferPosition.y);\n"
     "\t\tDrawFloorTileImpl(out, lightmap, tilePosition, targetBufferPosition);\n"
     "\t\tamiga_cells::ColumnCaptureEnd(1, tilePosition.x, tilePosition.y, key);\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"
     "\tDrawFloorTileImpl(out, lightmap, tilePosition, targetBufferPosition);\n"
     "}\n"
     "\n"
     "void DrawFloorTileImpl(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition)\n{\n\tconst int lightTableIndex = dLight[tilePosition.x][tilePosition.y];\n"),
    # Start from the icon: its Tool Types (or the same words in the Shell's
    # arguments) become the game's options (port/sdl/src/amiga_wb.c).
    ("Source/main.cpp",
     "\tconst int result = devilution::DiabloMain(argc, argv);\n",
     "#ifdef __amigaos__\n"
     "\tamiga_args(&argc, &argv);\n"
     "#endif\n"
     "\tconst int result = devilution::DiabloMain(argc, argv);\n"),
    ("Source/main.cpp",
     "extern \"C\" int main(int argc, char **argv)\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" void amiga_args(int *argc, char ***argv);\n"
     "#endif\n"
     "\n"
     "extern \"C\" int main(int argc, char **argv)\n"),
    ("Source/diablo.cpp",
     "\tLoadOptions();\n\tif (demo::IsRunning()) demo::OverrideOptions();\n",
     "\tLoadOptions();\n\tif (demo::IsRunning()) demo::OverrideOptions();\n"
     "#ifdef __amigaos__\n"
     "\tif (amiga_opt_showfps >= 0)\n"
     "\t\tGetOptions().Graphics.showFPS.SetValue(amiga_opt_showfps != 0);\n"
     "#endif\n"),
    ("Source/diablo.cpp",
     "int DiabloMain(int argc, char **argv)\n{\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" int amiga_opt_showfps; // Tool Type SHOWFPS (port/sdl/src/amiga_wb.c)\n"
     "#endif\n"
     "\n"
     "int DiabloMain(int argc, char **argv)\n{\n"),
    # Translations: the .gmo from the assets folder was read with a seek and a
    # read per string (thousands of AmigaDOS calls): de.gmo took 13 s, the
    # screen grey meanwhile. Read the whole file at once, as from an MPQ.
    ("Source/utils/language.cpp",
     "\tconst bool readWholeFile = handle.handle->type == SDL_RWOPS_UNKNOWN;\n",
     "#ifdef __amigaos__\n"
     "\tconst bool readWholeFile = true;\n"
     "#else\n"
     "\tconst bool readWholeFile = handle.handle->type == SDL_RWOPS_UNKNOWN;\n"
     "#endif\n"),
    # Release number (port/VERSION -> Source/amiga_version.h): in the main
    # menu's corner after the upstream version, and as $VER (Version command).
    ("Source/diablo.cpp",
     "\t*BufCopy(gszProductName, PROJECT_NAME, \" v\", PROJECT_VERSION) = '\\0';\n",
     "#ifdef __amigaos__\n"
     "\t*BufCopy(gszProductName, PROJECT_NAME, \" v\", PROJECT_VERSION, \" Warp \", DIABLO_WARP_VERSION) = '\\0';\n"
     "#else\n"
     "\t*BufCopy(gszProductName, PROJECT_NAME, \" v\", PROJECT_VERSION) = '\\0';\n"
     "#endif\n"),
    ("Source/diablo.cpp",
     "#include \"diablo.h\"\n",
     "#include \"diablo.h\"\n#ifdef __amigaos__\n#include \"amiga_version.h\"\n#include \"engine/palette.h\"\n#include \"utils/file_util.h\"\n#include <cctype>\n#include <cstdlib>\n#include <cstring>\n#endif\n"),
    ("Source/main.cpp",
     "#ifdef __amigaos__\nextern \"C\" void amiga_args(int *argc, char ***argv);\n#endif\n",
     "#ifdef __amigaos__\nextern \"C\" void amiga_args(int *argc, char ***argv);\n"
     "#include \"amiga_version.h\"\n"
     "extern \"C\" __attribute__((used)) const char amiga_ver[] = \"$VER: devilutionx \" DIABLO_WARP_VERSION \" (\" DIABLO_WARP_DATE \")\";\n"
     "#endif\n"),
    # F10: a report for a bug (PORTING_PLAN): the game saved as from its menu,
    # then the save, the settings and a short text with the version to
    # <save dir>/bugNNN/ (single player; F10 was a multiplayer quick message,
    # and this port has no network game).
    ("Source/diablo.cpp",
     "\t\t    (i < 4) ? static_cast<uint32_t>(SDLK_F9) + i : static_cast<uint32_t>(SDLK_UNKNOWN),\n",
     "#ifdef __amigaos__\n"
     "\t\t    (i < 4 && i != 1) ? static_cast<uint32_t>(SDLK_F9) + i : static_cast<uint32_t>(SDLK_UNKNOWN),\n"
     "#else\n"
     "\t\t    (i < 4) ? static_cast<uint32_t>(SDLK_F9) + i : static_cast<uint32_t>(SDLK_UNKNOWN),\n"
     "#endif\n"),
    ("Source/diablo.cpp",
     "\toptions.Keymapper.AddAction(\n\t    \"PauseGame\",\n",
     "#ifdef __amigaos__\n"
     "\toptions.Keymapper.AddAction(\n"
     "\t    \"BugReport\",\n"
     "\t    N_(\"Bug Report\"),\n"
     "\t    N_(\"Saves the game and a report for a bug in the save directory (drawer bugNNN).\"),\n"
     "\t    SDLK_F10,\n"
     "\t    AmigaBugReport,\n"
     "\t    nullptr,\n"
     "\t    CanPlayerTakeAction);\n"
     "#endif\n"
     "\toptions.Keymapper.AddAction(\n\t    \"PauseGame\",\n"),
    ("Source/diablo.cpp",
     "void SetApplicationVersions()\n{\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" const char *amiga_save_path(void);\n"
     "} // namespace (the report functions are called from control_chat.cpp)\n"
     "extern \"C\" int amiga_machine_info(char *buf, int size);\n"
     "extern \"C\" int Report_Send(const char *host, int port, const char *version, const char *kind, const char *email,\n"
     "    const char *text, const char *machine, const char *const *files, int nfiles);\n"
     "extern \"C\" int Report_Result(char *text, int size, int *ok);\n"
     "extern \"C\" int Report_Network(void);\n"
     "void AmigaOpenReportChat(const char *initial); // control_chat.cpp\n"
     "namespace {\n"
     "// F10 to the game server (GAME_SERVER.md): the chat bar asks for the e-mail\n"
     "// (remembered in <config dir>/report.ini), then the words line by line; an\n"
     "// empty line sends, Esc keeps the report on the disk only.\n"
     "int amigaReportStep = 0; // 1 e-mail, 2 words, 3 sending\n"
     "std::string amigaReportName, amigaReportFolder, amigaReportPath, amigaReportEmail, amigaReportText;\n"
     "constexpr size_t AmigaReportTextMax = 1000;\n"
     "std::string AmigaReportSetting(const char *key, const char *def)\n"
     "{\n"
     "\tstd::string value = def;\n"
     "\tif (FILE *f = std::fopen((paths::ConfigPath() + \"report.ini\").c_str(), \"r\")) {\n"
     "\t\tchar line[256];\n"
     "\t\tconst size_t klen = std::strlen(key);\n"
     "\t\twhile (std::fgets(line, sizeof(line), f) != nullptr) {\n"
     "\t\t\tif (std::strncmp(line, key, klen) == 0 && line[klen] == '=') {\n"
     "\t\t\t\tvalue = line + klen + 1;\n"
     "\t\t\t\twhile (!value.empty() && (value.back() == '\\n' || value.back() == '\\r' || value.back() == ' '))\n"
     "\t\t\t\t\tvalue.pop_back();\n"
     "\t\t\t}\n"
     "\t\t}\n"
     "\t\tstd::fclose(f);\n"
     "\t}\n"
     "\treturn value;\n"
     "}\n"
     "constexpr const char *AmigaReportServerDefault = \"83.12.152.135:6810\";\n"
     "/** Text of the player to latin-1 ASCII for the server: Polish and German letters without their marks. */\n"
     "std::string AmigaReportAscii(std::string_view in)\n"
     "{\n"
     "\tstatic const char *const map[][2] = { { \"\\xC4\\x85\", \"a\" }, { \"\\xC4\\x87\", \"c\" }, { \"\\xC4\\x99\", \"e\" }, { \"\\xC5\\x82\", \"l\" },\n"
     "\t\t{ \"\\xC5\\x84\", \"n\" }, { \"\\xC3\\xB3\", \"o\" }, { \"\\xC5\\x9B\", \"s\" }, { \"\\xC5\\xBA\", \"z\" }, { \"\\xC5\\xBC\", \"z\" },\n"
     "\t\t{ \"\\xC4\\x84\", \"A\" }, { \"\\xC4\\x86\", \"C\" }, { \"\\xC4\\x98\", \"E\" }, { \"\\xC5\\x81\", \"L\" }, { \"\\xC5\\x83\", \"N\" },\n"
     "\t\t{ \"\\xC3\\x93\", \"O\" }, { \"\\xC5\\x9A\", \"S\" }, { \"\\xC5\\xB9\", \"Z\" }, { \"\\xC5\\xBB\", \"Z\" }, { \"\\xC3\\xA4\", \"ae\" },\n"
     "\t\t{ \"\\xC3\\xB6\", \"oe\" }, { \"\\xC3\\xBC\", \"ue\" }, { \"\\xC3\\x9F\", \"ss\" }, { \"\\xC3\\x84\", \"Ae\" }, { \"\\xC3\\x96\", \"Oe\" }, { \"\\xC3\\x9C\", \"Ue\" } };\n"
     "\tstd::string out;\n"
     "\tfor (size_t i = 0; i < in.size();) {\n"
     "\t\tconst unsigned char c = static_cast<unsigned char>(in[i]);\n"
     "\t\tif (c < 0x80) {\n"
     "\t\t\tout += static_cast<char>(c);\n"
     "\t\t\ti++;\n"
     "\t\t\tcontinue;\n"
     "\t\t}\n"
     "\t\tbool found = false;\n"
     "\t\tfor (const auto &m : map) {\n"
     "\t\t\tif (in.substr(i, 2) == m[0]) {\n"
     "\t\t\t\tout += m[1];\n"
     "\t\t\t\ti += 2;\n"
     "\t\t\t\tfound = true;\n"
     "\t\t\t\tbreak;\n"
     "\t\t\t}\n"
     "\t\t}\n"
     "\t\tif (!found) {\n"
     "\t\t\tout += '?';\n"
     "\t\t\tdo i++; while (i < in.size() && (static_cast<unsigned char>(in[i]) & 0xC0) == 0x80);\n"
     "\t\t}\n"
     "\t}\n"
     "\treturn out;\n"
     "}\n"
     "void AmigaReportStart(const std::string &name, const std::string &folder, const std::string &path, bool saveOk)\n"
     "{\n"
     "\tconst std::string saved = (saveOk ? \"Report saved: \" : \"Report (no save copy): \") + folder;\n"
     "\tif (!Report_Network() || amigaReportStep != 0) {\n"
     "\t\tEventPlrMsg(saved + (amigaReportStep != 0 ? \" (the last report is still being sent)\" : \"\"), UiFlags::ColorWhite);\n"
     "\t\treturn;\n"
     "\t}\n"
     "\tamigaReportName = name;\n"
     "\tamigaReportFolder = folder;\n"
     "\tamigaReportPath = path;\n"
     "\tamigaReportText.clear();\n"
     "\tamigaReportStep = 1;\n"
     "\tEventPlrMsg(saved, UiFlags::ColorWhite);\n"
     "\tEventPlrMsg(\"To send it to the game server: your e-mail (may be empty), Enter. Esc: keep it on the disk only.\", UiFlags::ColorWhite);\n"
     "\tAmigaOpenReportChat(AmigaReportSetting(\"email\", \"\").c_str());\n"
     "}\n"
     "} // namespace\n"
     "/** The chat bar's Enter while it takes the report (control_chat.cpp). */\n"
     "void AmigaReportEnter(const std::string &line)\n"
     "{\n"
     "\tif (amigaReportStep == 1) {\n"
     "\t\tamigaReportEmail.clear();\n"
     "\t\tfor (const char c : line)\n"
     "\t\t\tif (std::isalnum(static_cast<unsigned char>(c)) || std::strchr(\"@._-+\", c) != nullptr)\n"
     "\t\t\t\tif (amigaReportEmail.size() < 80) amigaReportEmail += c;\n"
     "\t\tconst std::string server = AmigaReportSetting(\"server\", AmigaReportServerDefault);\n"
     "\t\tif (FILE *f = std::fopen((paths::ConfigPath() + \"report.ini\").c_str(), \"w\")) {\n"
     "\t\t\tstd::fprintf(f, \"email=%s\\nserver=%s\\n\", amigaReportEmail.c_str(), server.c_str());\n"
     "\t\t\tstd::fclose(f);\n"
     "\t\t}\n"
     "\t\tamigaReportStep = 2;\n"
     "\t\tEventPlrMsg(\"What happened? Enter after each line; an empty line sends the report. Esc: do not send.\", UiFlags::ColorWhite);\n"
     "\t\tAmigaOpenReportChat(\"\");\n"
     "\t\treturn;\n"
     "\t}\n"
     "\tif (amigaReportStep != 2)\n"
     "\t\treturn;\n"
     "\tif (!line.empty() && amigaReportText.size() < AmigaReportTextMax) {\n"
     "\t\tamigaReportText += AmigaReportAscii(line);\n"
     "\t\tamigaReportText += '\\n';\n"
     "\t\tif (amigaReportText.size() > AmigaReportTextMax) amigaReportText.resize(AmigaReportTextMax);\n"
     "\t\tEventPlrMsg(StrCat(amigaReportText.size(), \" / \", AmigaReportTextMax, \" - the next line, or an empty line to send\"), UiFlags::ColorWhite);\n"
     "\t\tAmigaOpenReportChat(\"\");\n"
     "\t\treturn;\n"
     "\t}\n"
     "\t// send: the words also into bugNNN.txt\n"
     "\tif (FILE *t = std::fopen((amigaReportPath + \".txt\").c_str(), \"a\")) {\n"
     "\t\tstd::fputs(amigaReportText.c_str(), t);\n"
     "\t\tstd::fclose(t);\n"
     "\t}\n"
     "\tstd::string server = AmigaReportSetting(\"server\", AmigaReportServerDefault);\n"
     "\tint port = 6810;\n"
     "\tif (const size_t colon = server.rfind(':'); colon != std::string::npos) {\n"
     "\t\tport = std::atoi(server.c_str() + colon + 1);\n"
     "\t\tserver.resize(colon);\n"
     "\t}\n"
     "\tchar machine[256];\n"
     "\tamiga_machine_info(machine, sizeof(machine));\n"
     "\tmachine[119] = '\\0'; // the server keeps 120\n"
     "\tconst std::string files[4] = { amigaReportPath + \".txt\", amigaReportPath + \"-screen.bmp\", amigaReportPath + \".sv\", amigaReportPath + \".ini\" };\n"
     "\tconst char *names[4] = { files[0].c_str(), files[1].c_str(), files[2].c_str(), files[3].c_str() };\n"
     "\tif (Report_Send(server.c_str(), port, \"Diablo-\" DIABLO_WARP_VERSION, \"F10\", amigaReportEmail.c_str(), amigaReportText.c_str(),\n"
     "\t        machine, names, 4)) {\n"
     "\t\tamigaReportStep = 3;\n"
     "\t\tEventPlrMsg(\"Sending the report to the game server...\", UiFlags::ColorWhite);\n"
     "\t} else {\n"
     "\t\tamigaReportStep = 0;\n"
     "\t\tEventPlrMsg(\"The report could not be sent. Files: \" + amigaReportFolder, UiFlags::ColorWhite);\n"
     "\t}\n"
     "}\n"
     "/** Esc (or anything else closing the chat bar) before the report was sent. */\n"
     "void AmigaReportCancelled()\n"
     "{\n"
     "\tif (amigaReportStep == 1 || amigaReportStep == 2) {\n"
     "\t\tamigaReportStep = 0;\n"
     "\t\tSDL_Log(\"Bug report %s: not sent (Esc)\", amigaReportName.c_str());\n"
     "\t\tEventPlrMsg(\"Report not sent. Files: \" + amigaReportFolder, UiFlags::ColorWhite);\n"
     "\t}\n"
     "}\n"
     "/** Every game tick: the result of the sending, when it is there. */\n"
     "void AmigaReportPoll()\n"
     "{\n"
     "\tif (amigaReportStep != 3)\n"
     "\t\treturn;\n"
     "\tchar text[80];\n"
     "\tint ok = 0;\n"
     "\tif (!Report_Result(text, sizeof(text), &ok))\n"
     "\t\treturn;\n"
     "\tamigaReportStep = 0;\n"
     "\tSDL_Log(\"Bug report %s: %s %s\", amigaReportName.c_str(), ok ? \"sent, number\" : \"not sent:\", text);\n"
     "\tif (ok)\n"
     "\t\tEventPlrMsg(StrCat(\"Report no. \", text, \" sent. Thank you!\"), UiFlags::ColorWhite);\n"
     "\telse\n"
     "\t\tEventPlrMsg(StrCat(\"Report not sent: \", text, \" Files: \", amigaReportFolder), UiFlags::ColorWhite);\n"
     "}\n"
     "namespace {\n"
     "/** F10: the game saved, then a bug report in its own drawer <save dir>/bugNNN/:\n"
     "    bugNNN.txt (version, machine, hero), bugNNN-screen.bmp (the last frame),\n"
     "    bugNNN.sv (the save) and bugNNN.ini (the settings). NNN = the first number\n"
     "    with no drawer bugNNN and no old file bugNNN.txt in the save dir; the hero\n"
     "    list does not show them (it reads single_N.sv of the save dir only). */\n"
     "void AmigaBugReport()\n"
     "{\n"
     "\tif (gbIsMultiplayer || !gbRunGame || MyPlayer == nullptr)\n"
     "\t\treturn;\n"
     "\tconst std::string base = paths::PrefPath();\n"
     "\tchar name[8];\n"
     "\tint num = -1;\n"
     "\tfor (int i = 1; i < 1000; i++) {\n"
     "\t\tstd::snprintf(name, sizeof(name), \"bug%03d\", i);\n"
     "\t\tif (!DirectoryExists(base + name) && !FileExists(base + name + \".txt\")) {\n"
     "\t\t\tnum = i;\n"
     "\t\t\tbreak;\n"
     "\t\t}\n"
     "\t}\n"
     "\tif (num < 0 || !CreateDir((base + name).c_str())) {\n"
     "\t\tEventPlrMsg(\"Report: no drawer for it in the save directory\", UiFlags::ColorWhite);\n"
     "\t\treturn;\n"
     "\t}\n"
     "\tconst std::string dir = base + name + \"/\" + name;\n"
     "\tconst auto copy = [](const std::string &from, const std::string &to) {\n"
     "\t\tFILE *in = std::fopen(from.c_str(), \"rb\");\n"
     "\t\tif (in == nullptr) return false;\n"
     "\t\tFILE *out = std::fopen(to.c_str(), \"wb\");\n"
     "\t\tif (out == nullptr) { std::fclose(in); return false; }\n"
     "\t\tchar buf[8192];\n"
     "\t\tsize_t n;\n"
     "\t\tbool ok = true;\n"
     "\t\twhile ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)\n"
     "\t\t\tif (std::fwrite(buf, 1, n, out) != n) ok = false;\n"
     "\t\tstd::fclose(in);\n"
     "\t\tif (std::fclose(out) != 0) ok = false;\n"
     "\t\treturn ok;\n"
     "\t};\n"
     "\t// the last frame (8-bit, the game's palette) as an uncompressed BMP,\n"
     "\t// before the save draws its \"Saving...\" box over it\n"
     "\tbool shot = false;\n"
     "\tif (PalSurface != nullptr && PalSurface->format->BitsPerPixel == 8) {\n"
     "\t\tif (FILE *f = std::fopen((dir + \"-screen.bmp\").c_str(), \"wb\")) {\n"
     "\t\t\tconst int w = PalSurface->w, h = PalSurface->h, row = (w + 3) & ~3;\n"
     "\t\t\tconst uint32_t data = 54 + 1024, size = data + static_cast<uint32_t>(row) * h;\n"
     "\t\t\tuint8_t hdr[54] = { 'B', 'M' };\n"
     "\t\t\tconst auto le = [&hdr](int at, uint32_t v, int bytes) { for (int k = 0; k < bytes; k++) hdr[at + k] = static_cast<uint8_t>(v >> (8 * k)); };\n"
     "\t\t\tle(2, size, 4); le(10, data, 4); le(14, 40, 4); le(18, w, 4); le(22, h, 4);\n"
     "\t\t\tle(26, 1, 2); le(28, 8, 2); le(34, static_cast<uint32_t>(row) * h, 4); le(46, 256, 4);\n"
     "\t\t\tstd::fwrite(hdr, 1, sizeof(hdr), f);\n"
     "\t\t\tfor (const SDL_Color &c : system_palette) {\n"
     "\t\t\t\tconst uint8_t q[4] = { c.b, c.g, c.r, 0 };\n"
     "\t\t\t\tstd::fwrite(q, 1, 4, f);\n"
     "\t\t\t}\n"
     "\t\t\tstd::vector<uint8_t> line(row, 0);\n"
     "\t\t\tfor (int y = h - 1; y >= 0; y--) {\n"
     "\t\t\t\tstd::memcpy(line.data(), static_cast<const uint8_t *>(PalSurface->pixels) + y * PalSurface->pitch, w);\n"
     "\t\t\t\tstd::fwrite(line.data(), 1, row, f);\n"
     "\t\t\t}\n"
     "\t\t\tshot = std::fclose(f) == 0;\n"
     "\t\t}\n"
     "\t}\n"
     "\tgamemenu_save_game(false);\n"
     "\tconst bool ok = copy(amiga_save_path(), dir + \".sv\");\n"
     "\tcopy(paths::ConfigPath() + \"diablo.ini\", dir + \".ini\");\n"
     "\tif (FILE *t = std::fopen((dir + \".txt\").c_str(), \"w\")) {\n"
     "\t\tchar machine[256];\n"
     "\t\tamiga_machine_info(machine, sizeof(machine));\n"
     "\t\tstd::fprintf(t, \"Bug report %s\\n\\nRelease: %s\\nGame: %s (%s data)\\nMachine: %s\\n\", name, gszProductName,\n"
     "\t\t    gbIsHellfire ? \"Hellfire\" : \"Diablo\", gbIsSpawn ? \"shareware\" : \"full\", machine);\n"
     "\t\tstd::fprintf(t, \"Hero: %s, character level %d; dungeon level %d (type %d%s)\\n\", MyPlayer->_pName,\n"
     "\t\t    MyPlayer->getCharacterLevel(), currlevel, static_cast<int>(leveltype), setlevel ? \", quest level\" : \"\");\n"
     "\t\tstd::fprintf(t, \"Files: %s.sv (copy of %s), %s.ini, %s-screen.bmp%s\\n\\n\", name, amiga_save_path(), name, name,\n"
     "\t\t    shot ? \"\" : \" (not written)\");\n"
     "\t\tstd::fprintf(t, \"What happened (write it here):\\n\\n\");\n"
     "\t\tstd::fclose(t);\n"
     "\t}\n"
     "\tAmigaReportStart(name, base + name, dir, ok);\n"
     "}\n"
     "#endif\n"
     "\n"
     "void SetApplicationVersions()\n{\n"),
    ("Source/pfile.cpp",
     "\tpfile_write_hero();\n\tsfile_write_stash();\n}\n\n} // namespace devilution\n",
     "\tpfile_write_hero();\n\tsfile_write_stash();\n}\n\n"
     "#ifdef __amigaos__\n"
     "/** The current game's save file (the F10 report, diablo.cpp). */\n"
     "extern \"C\" const char *amiga_save_path(void)\n"
     "{\n"
     "\tstatic std::string path;\n"
     "\tpath = GetSavePath(gSaveNumber);\n"
     "\treturn path.c_str();\n"
     "}\n"
     "#endif\n"
     "\n} // namespace devilution\n"),
    # F10 to the game server: the result of the sending, every game tick
    ("Source/diablo.cpp",
     "void GameLogic()\n{\n",
     "void GameLogic()\n{\n#ifdef __amigaos__\n\t::devilution::AmigaReportPoll(); // diablo.cpp, before SetApplicationVersions\n#endif\n"),
    # F10 to the game server: the chat bar (multiplayer only upstream) takes
    # the e-mail and the words of a bug report in single player too
    ("Source/control/control_chat.cpp",
     "OptionalOwnedClxSpriteList talkButtons;\n",
     "OptionalOwnedClxSpriteList talkButtons;\n"
     "#ifdef __amigaos__\n"
     "bool AmigaReportChat = false; // the chat bar takes a bug report's words (diablo.cpp)\n"
     "void AmigaReportEnter(const std::string &line);\n"
     "void AmigaReportCancelled();\n"
     "#endif\n"),
    ("Source/control/control_chat.cpp",
     "void ControlPressEnter()\n{\n",
     "void ControlPressEnter()\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (AmigaReportChat) {\n"
     "\t\tconst std::string line = TalkMessage;\n"
     "\t\tAmigaReportChat = false;\n"
     "\t\tResetChat();\n"
     "\t\tAmigaReportEnter(line);\n"
     "\t\treturn;\n"
     "\t}\n"
     "#endif\n"),
    ("Source/control/control_chat.cpp",
     "void ResetChat()\n{\n\tChatFlag = false;\n",
     "#ifdef __amigaos__\n"
     "/** The chat bar for a bug report, initial: what it starts with (the remembered e-mail). */\n"
     "void AmigaOpenReportChat(const char *initial)\n"
     "{\n"
     "\tAmigaReportChat = true;\n"
     "\tChatFlag = true;\n"
     "\t*BufCopy(TalkMessage, std::string_view(initial).substr(0, sizeof(TalkMessage) - 1)) = '\\0';\n"
     "\tChatCursor = {};\n"
     "\tChatCursor.position = std::strlen(TalkMessage);\n"
     "\tChatInputState.emplace(TextInputState::Options {\n"
     "\t    .value = TalkMessage,\n"
     "\t    .cursor = &ChatCursor,\n"
     "\t    .maxLength = sizeof(TalkMessage) - 1 });\n"
     "\tfor (bool &talkButtonDown : TalkButtonsDown) {\n"
     "\t\ttalkButtonDown = false;\n"
     "\t}\n"
     "\tsgbPlrTalkTbl = GetMainPanel().size.height + PanelPaddingHeight;\n"
     "\tRedrawEverything();\n"
     "\tTalkSaveIndex = NextTalkSave;\n"
     "\tSDL_Rect rect = MakeSdlRect(GetMainPanel().position.x + 200, GetMainPanel().position.y + 22, 0, 27);\n"
     "\tSDL_SetTextInputArea(ghMainWnd, &rect, /*cursor=*/0);\n"
     "\tSDLC_StartTextInput(ghMainWnd);\n"
     "}\n"
     "#endif\n"
     "\n"
     "void ResetChat()\n{\n"
     "#ifdef __amigaos__\n"
     "\tif (AmigaReportChat) {\n"
     "\t\tAmigaReportChat = false;\n"
     "\t\tAmigaReportCancelled();\n"
     "\t}\n"
     "#endif\n"
     "\tChatFlag = false;\n"),
    ("Source/control/control_chat.cpp",
     "bool IsChatAvailable()\n{\n\treturn gbIsMultiplayer;\n}\n",
     "bool IsChatAvailable()\n{\n"
     "#ifdef __amigaos__\n"
     "\treturn gbIsMultiplayer || AmigaReportChat;\n"
     "#else\n"
     "\treturn gbIsMultiplayer;\n"
     "#endif\n"
     "}\n"),
    # "Game saved" stayed on the screen for good: upstream shows it for
    # 1000 ms minus the time of the save (uint32_t). On a PC a save takes less
    # than a second; on the Amiga it takes seconds and the time wrapped around
    # (~49 days, until Esc or a click). Now: the rest of the second, at least 1 s.
    ("Source/gamemenu.cpp",
     "\tInitDiabloMsg(EMSG_GAME_SAVED, currentTime + 1000 - SDL_GetTicks());\n",
     "#ifdef __amigaos__\n"
     "\tconst uint32_t saveTime = SDL_GetTicks() - currentTime;\n"
     "\tInitDiabloMsg(EMSG_GAME_SAVED, saveTime < 1000 ? 1000 - saveTime : 1000);\n"
     "#else\n"
     "\tInitDiabloMsg(EMSG_GAME_SAVED, currentTime + 1000 - SDL_GetTicks());\n"
     "#endif\n"),
    # Videos on the 8-bit screen: the game cannot scale indexed output and
    # shows them at their size (~320x160); here whole multiples (2x),
    # every source pixel doubled across and every row repeated.
    ("Source/storm/storm_svid.cpp",
     "\t\tif (isIndexedOutputFormat\n\t\t    || outputSurface->w == static_cast<int>(SVidWidth)\n",
     "#ifdef __amigaos__\n"
     "\t\tif (isIndexedOutputFormat && SVidWidth != 0 && SVidHeight != 0\n"
     "\t\t    && static_cast<int>(SVidWidth * 2) <= outputSurface->w && static_cast<int>(SVidHeight * 2) <= outputSurface->h) {\n"
     "\t\t\tconst int f = std::min(outputSurface->w / static_cast<int>(SVidWidth), outputSurface->h / static_cast<int>(SVidHeight));\n"
     "\t\t\tconst int w = static_cast<int>(SVidWidth), h = static_cast<int>(SVidHeight);\n"
     "\t\t\tconst int ox = (outputSurface->w - w * f) / 2, oy = (outputSurface->h - h * f) / 2;\n"
     "\t\t\tconst SDL_Surface *src = SVidSurface.get();\n"
     "\t\t\tconst int dpitch = outputSurface->pitch;\n"
     "\t\t\tif (f == 2 && (w & 3) == 0 && (ox & 3) == 0 && (dpitch & 3) == 0 && (src->pitch & 3) == 0) {\n"
     "\t\t\t\t// 2x by long words (big endian): 4 source pixels -> 2 longs, written to both rows\n"
     "\t\t\t\tstatic uint16_t dbl[256];\n"
     "\t\t\t\tif (dbl[1] == 0)\n"
     "\t\t\t\t\tfor (int i = 0; i < 256; i++) dbl[i] = static_cast<uint16_t>(i * 0x0101);\n"
     "\t\t\t\tfor (int y = 0; y < h; y++) {\n"
     "\t\t\t\t\tconst uint32_t *s = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(src->pixels) + y * src->pitch);\n"
     "\t\t\t\t\tuint32_t *d0 = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(outputSurface->pixels) + (oy + y * 2) * dpitch + ox);\n"
     "\t\t\t\t\tuint32_t *d1 = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(d0) + dpitch);\n"
     "\t\t\t\t\tfor (int x = w >> 2; x != 0; x--) {\n"
     "\t\t\t\t\t\tconst uint32_t v = *s++;\n"
     "\t\t\t\t\t\tconst uint32_t a = (static_cast<uint32_t>(dbl[v >> 24]) << 16) | dbl[(v >> 16) & 0xFF];\n"
     "\t\t\t\t\t\tconst uint32_t b = (static_cast<uint32_t>(dbl[(v >> 8) & 0xFF]) << 16) | dbl[v & 0xFF];\n"
     "\t\t\t\t\t\td0[0] = a; d0[1] = b; d0 += 2;\n"
     "\t\t\t\t\t\td1[0] = a; d1[1] = b; d1 += 2;\n"
     "\t\t\t\t\t}\n"
     "\t\t\t\t}\n"
     "\t\t\t\tRenderPresent();\n"
     "\t\t\t\treturn true;\n"
     "\t\t\t}\n"
     "\t\t\tfor (int y = 0; y < h; y++) {\n"
     "\t\t\t\tconst uint8_t *s = static_cast<const uint8_t *>(src->pixels) + y * src->pitch;\n"
     "\t\t\t\tuint8_t *d = static_cast<uint8_t *>(outputSurface->pixels) + (oy + y * f) * outputSurface->pitch + ox;\n"
     "\t\t\t\tif (f == 2) {\n"
     "\t\t\t\t\tuint16_t *d2 = reinterpret_cast<uint16_t *>(d);\n"
     "\t\t\t\t\tfor (int x = 0; x < w; x++)\n"
     "\t\t\t\t\t\td2[x] = static_cast<uint16_t>(s[x] * 0x0101);\n"
     "\t\t\t\t} else {\n"
     "\t\t\t\t\tfor (int x = 0; x < w; x++)\n"
     "\t\t\t\t\t\tfor (int k = 0; k < f; k++)\n"
     "\t\t\t\t\t\t\td[x * f + k] = s[x];\n"
     "\t\t\t\t}\n"
     "\t\t\t\tfor (int k = 1; k < f; k++)\n"
     "\t\t\t\t\tstd::memcpy(d + k * outputSurface->pitch, d, static_cast<size_t>(w * f));\n"
     "\t\t\t}\n"
     "\t\t\tRenderPresent();\n"
     "\t\t\treturn true;\n"
     "\t\t}\n"
     "#endif\n"
     "\t\tif (isIndexedOutputFormat\n\t\t    || outputSurface->w == static_cast<int>(SVidWidth)\n"),
    # Option "Light Steps" (user decision 4.11.2026): walking in the dungeon,
    # the player's light moves smoothly every game tick (UpdatePlayerLightOffset)
    # and nearly the whole picture changes; with steps it stays on the tile the
    # player left and jumps on arrival (the game's own ChangeLightXY + offset 0),
    # so between tiles only the camera moves (cell renderer: cheap). Off = the
    # original (default), On = always, Auto = when a walk runs below the FPS
    # threshold (measured once 0.5 s into the walk, kept until the player stops).
    ("Source/options.h",
     "enum class FrameRateControl : uint8_t {\n",
     "enum class LightSteps : uint8_t {\n\tOff,\n\tOn,\n\tAuto,\n};\n\nenum class FrameRateControl : uint8_t {\n"),
    ("Source/options.h",
     "\t/** @brief Subtile lighting for smoother light gradients. */\n\tOptionEntryBoolean perPixelLighting;\n",
     "\t/** @brief Subtile lighting for smoother light gradients. */\n\tOptionEntryBoolean perPixelLighting;\n"
     "\t/** @brief Warp port: the player's light moves tile by tile when walking (Off / On / Auto). */\n"
     "\tOptionEntryEnum<LightSteps> lightSteps;\n"
     "\t/** @brief Warp port: Auto light steps below this frame rate. */\n"
     "\tOptionEntryInt<int> lightStepsFps;\n"),
    ("Source/options.cpp",
     "    , perPixelLighting(\"Per-pixel Lighting\", OptionEntryFlags::None, N_(\"Per-pixel Lighting\"), N_(\"Subtile lighting for smoother light gradients.\"), DEFAULT_PER_PIXEL_LIGHTING)\n",
     "    , perPixelLighting(\"Per-pixel Lighting\", OptionEntryFlags::None, N_(\"Per-pixel Lighting\"), N_(\"Subtile lighting for smoother light gradients.\"), DEFAULT_PER_PIXEL_LIGHTING)\n"
     "    , lightSteps(\"Light Steps\", OptionEntryFlags::None, N_(\"Light Steps\"), N_(\"The player's light moves tile by tile when walking (faster walking on slow machines). Auto: when the frame rate is below the threshold.\"), LightSteps::Off,\n"
     "          {\n"
     "              { LightSteps::Off, N_(\"Off\") },\n"
     "              { LightSteps::On, N_(\"On\") },\n"
     "              { LightSteps::Auto, N_(\"Auto\") },\n"
     "          })\n"
     "    , lightStepsFps(\"Light Steps Below FPS\", OptionEntryFlags::None, N_(\"Light Steps Below FPS\"), N_(\"Auto light steps when walking runs below this frame rate.\"), 20, { 15, 20, 25, 30 })\n"),
    ("Source/options.cpp",
     "\t\t&perPixelLighting,\n",
     "\t\t&perPixelLighting,\n\t\t&lightSteps,\n\t\t&lightStepsFps,\n"),
    ("Source/player.cpp",
     "void UpdatePlayerLightOffset(Player &player)\n{\n\tif (player.lightId == NO_LIGHT)\n\t\treturn;\n",
     "#ifdef __amigaos__\n"
     "extern \"C\" unsigned msdl_recent_fps10(void);\n"
     "/** Light steps option: true when the light stays on its tile during this step. */\n"
     "bool AmigaLightSteps()\n"
     "{\n"
     "\tconst LightSteps mode = *GetOptions().Graphics.lightSteps;\n"
     "\tif (mode == LightSteps::Off)\n"
     "\t\treturn false;\n"
     "\tif (mode == LightSteps::On)\n"
     "\t\treturn true;\n"
     "\t// Auto: one decision per walk (no switching back and forth)\n"
     "\tstatic uint32_t lastTick, walkStart;\n"
     "\tstatic bool stepping;\n"
     "\tconst uint32_t now = SDL_GetTicks();\n"
     "\tif (now - lastTick > 250) {\n"
     "\t\twalkStart = now;\n"
     "\t\tstepping = false;\n"
     "\t}\n"
     "\tlastTick = now;\n"
     "\tif (!stepping && now - walkStart >= 500) {\n"
     "\t\tconst unsigned fps10 = msdl_recent_fps10();\n"
     "\t\tif (fps10 != 0 && fps10 < static_cast<unsigned>(*GetOptions().Graphics.lightStepsFps) * 10)\n"
     "\t\t\tstepping = true;\n"
     "\t}\n"
     "\treturn stepping;\n"
     "}\n"
     "#endif\n"
     "\n"
     "void UpdatePlayerLightOffset(Player &player)\n{\n\tif (player.lightId == NO_LIGHT)\n\t\treturn;\n"
     "#ifdef __amigaos__\n"
     "\tif (&player == MyPlayer && AmigaLightSteps())\n"
     "\t\treturn;\n"
     "#endif\n"),
    # The cell renderer learns when the light really changed (direct drawing
    # only while it keeps changing).
    ("Source/lighting.cpp",
     "\tif (!UpdateLighting)\n\t\treturn;\n\tfor (int i = 0; i < ActiveLightCount; i++) {\n",
     "\tif (!UpdateLighting)\n\t\treturn;\n#ifdef __amigaos__\n\tamiga_cells::lightChanges++;\n#endif\n\tfor (int i = 0; i < ActiveLightCount; i++) {\n"),
    ("Source/lighting.cpp",
     "#include \"engine/displacement.hpp\"\n",
     "#include \"engine/displacement.hpp\"\n#include \"engine/render/amiga_cells.hpp\"\n"),
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
    # Loose mods (mods/hf = Hellfire) next to the program: upstream looks
    # only in the save and data directories, while the release keeps the
    # game data in PROGDIR:data and mods/ beside the program; without this
    # Hellfire never switches on (14.11.2026).
    ("Source/engine/assets.cpp",
     "\t\ttargetPath = StrCat(paths::BasePath(), \"mods\" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);\n"
     "\t\tif (DirectoryExists(targetPath)) {\n"
     "\t\t\tOverridePaths.emplace_back(targetPath);\n"
     "#ifndef UNPACKED_MPQS\n"
     "\t\t\thasLooseOverride[i] = true;\n"
     "#endif\n"
     "\t\t}\n",
     "\t\ttargetPath = StrCat(paths::BasePath(), \"mods\" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);\n"
     "\t\tif (DirectoryExists(targetPath)) {\n"
     "\t\t\tOverridePaths.emplace_back(targetPath);\n"
     "#ifndef UNPACKED_MPQS\n"
     "\t\t\thasLooseOverride[i] = true;\n"
     "#endif\n"
     "\t\t}\n"
     "#if defined(__amigaos__) && !defined(UNPACKED_MPQS)\n"
     "\t\ttargetPath = StrCat(\"PROGDIR:mods/\", modname, \"/\");\n"
     "\t\tif (!hasLooseOverride[i] && DirectoryExists(targetPath)) {\n"
     "\t\t\tOverridePaths.emplace_back(targetPath);\n"
     "\t\t\thasLooseOverride[i] = true;\n"
     "\t\t}\n"
     "#endif\n"),
    # Videos: decoded / shown frames and the decoding time, in the log at
    # the end of each video (the Hellfire intro showed two frames, 14.11.2026).
    ("Source/storm/storm_svid.cpp",
     "bool SVidLoadNextFrame()\n{\n",
     "#ifdef __amigaos__\n"
     "uint32_t AmigaSVidDecoded, AmigaSVidShown, AmigaSVidDecodeMs, AmigaSVidStartMs, AmigaSVidSum;\n"
     "constexpr bool AmigaSVidNoSkip = true;\n"
     "} // namespace\n"
     "} // namespace devilution\n"
     "extern \"C\" {\n"
     "int amiga_snd_stream_start(int slot, int stereo, int vl, int vr);\n"
     "void amiga_snd_stream_end(int slot);\n"
     "void amiga_snd_stream_prime(int slot, long frames);\n"
     "long amiga_snd_stream_space(int slot);\n"
     "void amiga_snd_stream_write(int slot, const signed char *frames, long n);\n"
     "}\n"
     "namespace devilution {\n"
     "namespace {\n"
     "// Video sound on the Amiga (the build has NOSOUND, no SDL_audiolib): the\n"
     "// decoded audio of every frame goes to stream 2 of the Paula mixer\n"
     "// (port/sdl/src/amiga_sound.c) as signed 8-bit, 22050 Hz, mono or stereo.\n"
     "constexpr int AmigaSVidSlot = 2;\n"
     "bool AmigaSVidAudioOn;\n"
     "uint32_t AmigaSVidBytes, AmigaSVidChannels, AmigaSVidStep, AmigaSVidFrac, AmigaSVidOutMax, AmigaSVidLost, AmigaSVidSkipped;\n"
     "std::unique_ptr<int16_t[]> AmigaSVidIn;\n"
     "std::unique_ptr<signed char[]> AmigaSVidOut;\n"
     "\n"
     "void AmigaSVidAudioBegin(bool enable)\n"
     "{\n"
     "\tAmigaSVidAudioOn = false;\n"
     "\tAmigaSVidLost = 0;\n"
     "\tif (!enable)\n"
     "\t\treturn;\n"
     "\tconst auto info = Smacker_GetAudioTrackDetails(SVidHandle, 0);\n"
     "\tLogVerbose(\"video sound: {} bit, {} channels, {} Hz\", info.bitsPerSample, info.nChannels, info.sampleRate);\n"
     "\tif ((info.bitsPerSample != 8 && info.bitsPerSample != 16) || info.nChannels < 1 || info.nChannels > 2\n"
     "\t    || info.sampleRate < 4000 || info.idealBufferSize == 0)\n"
     "\t\treturn;\n"
     "\tAmigaSVidBytes = info.bitsPerSample / 8;\n"
     "\tAmigaSVidChannels = info.nChannels;\n"
     "\tAmigaSVidStep = (info.sampleRate << 16) / 22050; // source frames per output frame, 16.16\n"
     "\tAmigaSVidFrac = 0;\n"
     "\tconst uint32_t inFrames = info.idealBufferSize / (AmigaSVidBytes * AmigaSVidChannels);\n"
     "\tAmigaSVidOutMax = static_cast<uint32_t>((static_cast<uint64_t>(inFrames) << 16) / AmigaSVidStep) + 2;\n"
     "\tAmigaSVidIn.reset(new int16_t[info.idealBufferSize / 2 + 2]);\n"
     "\tAmigaSVidOut.reset(new signed char[AmigaSVidOutMax * AmigaSVidChannels]);\n"
     "\t// volume: the sound effects' setting, linear as upstream's GetVolume\n"
     "\tconst int vol = std::clamp((*GetOptions().Audio.soundVolume - VOLUME_MIN) * 64 / -VOLUME_MIN, 0, 64);\n"
     "\tif (amiga_snd_stream_start(AmigaSVidSlot, AmigaSVidChannels == 2, vol, vol) != 0)\n"
     "\t\treturn;\n"
     "\t// the sound comes just in time with each frame: ~190 ms reserve against gaps\n"
     "\tamiga_snd_stream_prime(AmigaSVidSlot, 4096);\n"
     "\tAmigaSVidAudioOn = true;\n"
     "}\n"
     "\n"
     "void AmigaSVidAudioPush()\n"
     "{\n"
     "\tif (!AmigaSVidAudioOn)\n"
     "\t\treturn;\n"
     "\tconst uint32_t len = Smacker_GetAudioData(SVidHandle, 0, AmigaSVidIn.get());\n"
     "\tconst uint32_t ch = AmigaSVidChannels, n = len / (AmigaSVidBytes * ch);\n"
     "\tif (n == 0)\n"
     "\t\treturn;\n"
     "\tconst int16_t *in16 = AmigaSVidIn.get();\n"
     "\tconst uint8_t *in8 = reinterpret_cast<const uint8_t *>(AmigaSVidIn.get());\n"
     "\tsigned char *out = AmigaSVidOut.get();\n"
     "\tuint32_t pos = AmigaSVidFrac, frames = 0;\n"
     "\tconst uint32_t end = n << 16;\n"
     "\twhile (pos < end && frames < AmigaSVidOutMax) {\n"
     "\t\tconst uint32_t k = (pos >> 16) * ch;\n"
     "\t\tfor (uint32_t c = 0; c < ch; c++)\n"
     "\t\t\t*out++ = AmigaSVidBytes == 2 ? static_cast<signed char>(in16[k + c] >> 8)\n"
     "\t\t\t                            : static_cast<signed char>(in8[k + c] ^ 0x80); // 8 bit: unsigned\n"
     "\t\tframes++;\n"
     "\t\tpos += AmigaSVidStep;\n"
     "\t}\n"
     "\tAmigaSVidFrac = pos >= end ? pos - end : 0;\n"
     "\tconst long space = amiga_snd_stream_space(AmigaSVidSlot);\n"
     "\tconst long put = std::min<long>(space, frames);\n"
     "\tAmigaSVidLost += frames - put;\n"
     "\tamiga_snd_stream_write(AmigaSVidSlot, AmigaSVidOut.get(), put);\n"
     "}\n"
     "\n"
     "// A frame late by two frames is not shown (its sound is already out; the\n"
     "// sound has ~190 ms reserve, so a smaller delay is caught up by the next\n"
     "// frames), at most 2 in a row: the picture keeps up with the sound and\n"
     "// never stays black.\n"
     "int AmigaSVidSkipRow;\n"
     "bool AmigaSVidSkip()\n"
     "{\n"
     "\tif (GetTicksSmk() < SVidFrameEnd + 2 * SVidFrameLength || AmigaSVidSkipRow >= 2) {\n"
     "\t\tAmigaSVidSkipRow = 0;\n"
     "\t\treturn false;\n"
     "\t}\n"
     "\tAmigaSVidSkipRow++;\n"
     "\tAmigaSVidSkipped++;\n"
     "\treturn true;\n"
     "}\n"
     "\n"
     "void AmigaSVidAudioEnd()\n"
     "{\n"
     "\tif (AmigaSVidAudioOn)\n"
     "\t\tamiga_snd_stream_end(AmigaSVidSlot); // the rest in the ring plays out\n"
     "\tAmigaSVidAudioOn = false;\n"
     "\tAmigaSVidIn = nullptr;\n"
     "\tAmigaSVidOut = nullptr;\n"
     "}\n"
     "#else\n"
     "constexpr bool AmigaSVidNoSkip = false;\n"
     "#endif\n"
     "bool SVidLoadNextFrame()\n{\n"),
    ("Source/storm/storm_svid.cpp",
     "\tSVidFrameEnd += SVidFrameLength;\n\n\tSmacker_GetNextFrame(SVidHandle);\n\tSmacker_GetFrame(SVidHandle, SVidFrameBuffer.get());\n",
     "\tSVidFrameEnd += SVidFrameLength;\n\n"
     "#ifdef __amigaos__\n"
     "\tconst uint32_t amigaT0 = SDL_GetTicks();\n"
     "#endif\n"
     "\tSmacker_GetNextFrame(SVidHandle);\n\tSmacker_GetFrame(SVidHandle, SVidFrameBuffer.get());\n"
     "#ifdef __amigaos__\n"
     "\tAmigaSVidDecodeMs += SDL_GetTicks() - amigaT0;\n"
     "\tAmigaSVidDecoded++;\n"
     "\t{ // checksum of every decoded frame: the decoder's output is compared between builds\n"
     "\t\tconst uint32_t *p = reinterpret_cast<const uint32_t *>(SVidFrameBuffer.get());\n"
     "\t\tfor (uint32_t k = SVidWidth * SVidHeight / 4; k != 0; k--)\n"
     "\t\t\tAmigaSVidSum = (AmigaSVidSum << 5) + (AmigaSVidSum >> 27) + *p++;\n"
     "\t}\n"
     "#endif\n"),
    # The Smacker decoder from port/overlay/3rdParty/libsmackerdec (the
    # same library with a fast bit reader for the 68060) instead of the
    # download.
    ("3rdParty/libsmackerdec/CMakeLists.txt",
     "FetchContent_MakeAvailable_ExcludeFromAll(libsmackerdec)\n",
     "if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/BitReader.cpp)\n"
     "  set(libsmackerdec_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})\n"
     "else()\n"
     "  FetchContent_MakeAvailable_ExcludeFromAll(libsmackerdec)\n"
     "endif()\n"),
    # Late frames are shown anyway: every frame has to be decoded (Smacker
    # frames are changes to the one before), so skipping the blit saves only
    # a few ms; with the Hellfire logo (97 ms decoding per 66 ms frame on a
    # 68060) upstream showed 1 frame of 128 and dropped the sound of the rest.
    ("Source/storm/storm_svid.cpp",
     "\tif (GetTicksSmk() >= SVidFrameEnd) {\n"
     "#if defined(USE_SDL3) && !defined(NOSOUND)\n",
     "#ifdef __amigaos__\n"
     "\tAmigaSVidAudioPush(); // always: the sound is the clock\n"
     "\tif (AmigaSVidSkip()) {\n"
     "#else\n"
     "\tif (GetTicksSmk() >= SVidFrameEnd) {\n"
     "#endif\n"
     "#if defined(USE_SDL3) && !defined(NOSOUND)\n"),
    ("Source/storm/storm_svid.cpp",
     "\tif (GetTicksSmk() >= SVidFrameEnd) {\n"
     "\t\treturn SVidLoadNextFrame(); // Skip video if the system is to slow\n",
     "\tif (!AmigaSVidNoSkip && GetTicksSmk() >= SVidFrameEnd) {\n"
     "\t\treturn SVidLoadNextFrame(); // Skip video if the system is to slow\n"),
    ("Source/storm/storm_svid.cpp",
     "\tif (!BlitFrame())\n\t\treturn false;\n",
     "#ifdef __amigaos__\n"
     "\tAmigaSVidShown++;\n"
     "#endif\n"
     "\tif (!BlitFrame())\n\t\treturn false;\n"),
    ("Source/storm/storm_svid.cpp",
     "\tSVidFrameEnd = GetTicksSmk() + SVidFrameLength;\n",
     "\tSVidFrameEnd = GetTicksSmk() + SVidFrameLength;\n"
     "#ifdef __amigaos__\n"
     "\tAmigaSVidDecoded = AmigaSVidShown = AmigaSVidDecodeMs = AmigaSVidSum = AmigaSVidSkipped = 0;\n"
     "\tAmigaSVidSkipRow = 0;\n"
     "\tAmigaSVidStartMs = SDL_GetTicks();\n"
     "#endif\n"),
    ("Source/storm/storm_svid.cpp",
     "\tif (!SVidHandle.isValid) {\n\t\treturn false;\n\t}\n\n#ifndef NOSOUND\n",
     "\tif (!SVidHandle.isValid) {\n\t\treturn false;\n\t}\n\n"
     "#ifdef __amigaos__\n"
     "\tAmigaSVidAudioBegin((flags & 0x1000000) == 0);\n"
     "#endif\n"
     "#ifndef NOSOUND\n"),
    ("Source/storm/storm_svid.cpp",
     "void SVidPlayEnd()\n{\n",
     "void SVidPlayEnd()\n{\n"
     "#ifdef __amigaos__\n"
     "\tAmigaSVidAudioEnd();\n"
     "\tLogInfo(\"video sound: {} frames lost (ring full), {} late frames not shown\", AmigaSVidLost, AmigaSVidSkipped);\n"
     "\tLogInfo(\"video {}x{}, frame {} ms: {} frames decoded ({} ms each), {} shown, {} ms, sum {:08x}\",\n"
     "\t    SVidWidth, SVidHeight, SVidFrameLength / 100, AmigaSVidDecoded,\n"
     "\t    AmigaSVidDecoded != 0 ? AmigaSVidDecodeMs / AmigaSVidDecoded : 0, AmigaSVidShown,\n"
     "\t    SDL_GetTicks() - AmigaSVidStartMs, AmigaSVidSum);\n"
     "#endif\n"),
]


def apply(tree, upstream):
    """Every changed file is made again from the clean upstream copy and
    written only when the result differs (no needless rebuilds)."""
    here = os.path.dirname(os.path.abspath(__file__))
    results = {}
    skip = [x for x in os.environ.get("APPLY_SKIP", "").split(",") if x]
    for rel, old, new in CHANGES:
        if rel not in results:
            with open(os.path.join(upstream, rel), "rb") as f:
                results[rel] = f.read().decode("utf-8")
        if rel in skip:
            continue        # A/B builds: APPLY_SKIP=Source/engine/path.cpp (upstream text)
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
    # the release number (port/VERSION, one line "0.xx") for the menu and $VER
    import datetime
    version = open(os.path.join(here, "VERSION")).read().strip()
    header = ('#pragma once\n// made by port/apply_port.py from port/VERSION\n'
              '#define DIABLO_WARP_VERSION "%s"\n#define DIABLO_WARP_DATE "%s"\n'
              % (version, datetime.date.today().strftime("%d.%m.%Y")))
    vpath = os.path.join(tree, "Source", "amiga_version.h")
    if not os.path.exists(vpath) or open(vpath).read() != header:
        with open(vpath, "w") as f:
            f.write(header)
        print("version", version)
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
