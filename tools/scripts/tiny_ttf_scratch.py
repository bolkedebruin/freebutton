"""
Compile LVGL's lv_tiny_ttf.c with lv_malloc / lv_free renamed to
fb_tiny_ttf_malloc / fb_tiny_ttf_free (src/ui/fonts/fonts.c).

lv_tiny_ttf.c defines STBTT_malloc / STBTT_free as lv_malloc / lv_free
unconditionally, so stb_truetype's rasterizer scratch (up to ~12 KB per glyph
at 72 px, freed before the glyph is returned) would come from the small LVGL
pool. fonts.c routes those calls to the system heap while a glyph is being
rasterized and passes every other call (font descriptors) on to the pool.

A per-file define instead of an edit of the library in .pio/libdeps: nothing
to re-apply after a library reinstall, and no other LVGL file is affected.
"""

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)


def _scratch_on_heap(env, node):
    # One node, not the NodeList: the ESP-IDF builder's own middleware runs
    # after this one and calls node.srcnode().
    return env.Object(
        node,
        CPPDEFINES=list(env["CPPDEFINES"]) + [("lv_malloc", "fb_tiny_ttf_malloc"), ("lv_free", "fb_tiny_ttf_free")],
    )[0]


env.AddBuildMiddleware(_scratch_on_heap, "*lv_tiny_ttf.c")  # noqa: F821
