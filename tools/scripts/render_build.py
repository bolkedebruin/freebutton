"""
Per-file compile settings for the rendering hot path (one build middleware:
two middlewares cannot both rewrite the same file, the second one would get
the first one's object node).

1. Optimization experiments (off by default): FB_RENDER_OPT=-O2 (or -O3,
   ...) compiles LVGL's renderer and font code and LovyanGFX at that level
   instead of the global one (every -O flag of the file is replaced).
   Measured on .81 (docs/PERFORMANCE.md): -O2 is no faster than -Os, with
   DIO or QIO flash, and the firmware grows by ~76 KB, so it stays off.

2. lv_tiny_ttf.c is compiled with lv_malloc / lv_free renamed to
   fb_tiny_ttf_malloc / fb_tiny_ttf_free (src/ui/fonts/fonts.c): stb_truetype
   defines STBTT_malloc as lv_malloc unconditionally, and its rasterizer
   scratch (up to ~12 KB per glyph) must come from the system heap, not the
   small LVGL pool. A per-file define instead of an edit of the library in
   .pio/libdeps: nothing to re-apply after a library reinstall.
"""
import fnmatch
import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

RENDER_OPT = os.environ.get("FB_RENDER_OPT", "off")

# Source paths (as SCons reports them, matched with fnmatch).
HOT = [
    "*/lvgl/src/draw/*",
    "*/lvgl/src/misc/*",
    "*/lvgl/src/core/*",
    "*/lvgl/src/font/*",
    "*/lvgl/src/libs/bin_decoder/*",
    "*/lvgl/src/widgets/label/*",
    "*/lvgl/src/stdlib/*",
    "*/LovyanGFX/*",
]
# The project's own sources (src/) are built by ESP-IDF's CMake, not by
# PlatformIO: their render-path flags are set in src/CMakeLists.txt.


def _is_hot(path):
    return RENDER_OPT != "off" and any(fnmatch.fnmatch(path, p) for p in HOT)


def _replace_opt(flags, add):
    out = [f for f in flags if not (isinstance(f, str) and f.startswith("-O"))]
    return out + [RENDER_OPT] if add else out


def _render_build(env, node):
    path = node.srcnode().get_path()
    tiny_ttf = path.endswith("lv_tiny_ttf.c")
    hot = _is_hot(path)
    if not (tiny_ttf or hot):
        return node
    overrides = {}
    if tiny_ttf:
        overrides["CPPDEFINES"] = list(env["CPPDEFINES"]) + [("lv_malloc", "fb_tiny_ttf_malloc"),
                                                             ("lv_free", "fb_tiny_ttf_free")]
    if hot:
        # every -O goes; the level is added once, last (CCFLAGS follows
        # CFLAGS / CXXFLAGS on the command line)
        for var in ("CFLAGS", "CXXFLAGS"):
            overrides[var] = _replace_opt(list(env.get(var, [])), False)
        overrides["CCFLAGS"] = _replace_opt(list(env.get("CCFLAGS", [])), True)
    # One node, not the NodeList: the ESP-IDF builder's own middleware runs
    # after this one and calls node.srcnode().
    return env.Object(node, **overrides)[0]


env.AddBuildMiddleware(_render_build)  # noqa: F821
