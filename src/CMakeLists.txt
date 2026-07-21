# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# The FreeButton JS bindings in src/scripting/js/ are #included into
# mquickjs_stdlib.c, not compiled as standalone translation units.
list(FILTER app_sources EXCLUDE REGEX "/scripting/js/")

idf_component_register(SRCS ${app_sources})
