"""
Strip empty entries from CPATH / CPLUS_INCLUDE_PATH / C_INCLUDE_PATH before the
native host compile runs.

macOS's filesystem is case-insensitive, so libc++'s `<version>` header collides
with the repo-root `VERSION` file (the single version source of truth). An empty
element in CPATH — e.g. a trailing colon, `CPATH=/opt/homebrew/include:` — is
interpreted by the compiler as the current working directory. Since PlatformIO
runs from the repo root, that puts `.` on the angle-include search path and
`#include <version>` (pulled in transitively by `<string>`/`<memory>`) resolves
to `./VERSION`, failing every native compile with "expected unqualified-id".

This is a host-only trap: on Linux CI the FS is case-sensitive so `<version>`
never matches `VERSION`. Removing the empty entries keeps `/opt/homebrew/include`
while dropping the implicit `.`, and is a no-op when CPATH is already clean.

See the `version-header-shadow` project memory for the full background.
"""

import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)


def _sanitize(var_name):
    value = os.environ.get(var_name)
    if not value:
        return
    parts = [p for p in value.split(os.pathsep) if p]
    cleaned = os.pathsep.join(parts)
    if cleaned != value:
        os.environ[var_name] = cleaned
        print(f"[sanitize_cpath] {var_name}: dropped empty (CWD) entries -> '{cleaned}'")


for _name in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "OBJC_INCLUDE_PATH"):
    _sanitize(_name)
