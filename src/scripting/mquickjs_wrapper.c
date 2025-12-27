/*
 * MicroQuickJS Library Wrapper
 *
 * This file includes all necessary mquickjs source files to ensure they're
 * compiled as part of the main firmware build. This works around PlatformIO
 * library build issues with the arduino+espidf dual framework.
 *
 * Build order:
 * 1. Core mquickjs runtime (includes cutils.h and dtoa.h as headers)
 * 2. Utility libraries (cutils, dtoa, libm - compiled separately)
 * 3. FreeButton LED bindings
 * 4. Stdlib export with our custom stdlib
 */

// Core mquickjs runtime
#include "../../lib/mquickjs/mquickjs.c"
