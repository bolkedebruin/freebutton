#!/usr/bin/env python3
"""List the project files the emulator wasm was compiled from.

usage: list_sources.py <project root> <build dir with the compilers' .d files>

Prints, sorted, every file of the dependency lists that is inside the
project: firmware sources and headers (src/, include/, lib/vendored/) and
the emulator's own (tools/js/emulator/). Not listed: the engine submodule
(its commit is in the stamp), the generated stdlib headers (their hashes are
in the stamp) and the libraries PlatformIO fetches (their versions are pinned
in platformio.ini, which the stamp also reads).
"""
import glob
import os
import sys


def main():
    root = os.path.realpath(sys.argv[1])
    build = sys.argv[2]
    found = set()
    for dep in glob.glob(os.path.join(build, "*.d")):
        with open(dep) as f:
            text = f.read().replace("\\\n", " ")
        for word in text.split()[1:]:
            path = os.path.realpath(word)
            if not path.startswith(root + os.sep):
                continue
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            if rel.startswith((".pio/", "lib/mquickjs/")):
                continue
            found.add(rel)
    if not found:
        sys.exit("no dependency lists in " + build)
    print("\n".join(sorted(found)))


if __name__ == "__main__":
    main()
