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
