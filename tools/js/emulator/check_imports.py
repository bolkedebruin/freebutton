#!/usr/bin/env python3
"""Compare the imports of fb-emulator.wasm with tools/js/emulator/imports.txt.

The emulator wasm is linked with undefined symbols allowed: the seam functions
the host implements stay undefined on purpose and become wasm imports. This
check makes that list explicit, so a function that is undefined by mistake (a
firmware file that was not compiled in, a new seam function the host does not
have yet) fails the build instead of the page.

usage:
  check_imports.py <fb-emulator.wasm> <imports.txt>            compare; exit 1 on a difference
  check_imports.py <fb-emulator.wasm> <imports.txt> --write    write the list (review the diff)
"""
import sys


def leb(data, pos):
    result = 0
    shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return result, pos


def name(data, pos):
    length, pos = leb(data, pos)
    return data[pos:pos + length].decode("utf-8"), pos + length


def limits(data, pos):
    flags, pos = leb(data, pos)
    _, pos = leb(data, pos)
    if flags & 1:
        _, pos = leb(data, pos)
    return pos


def imports(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\0asm\x01\0\0\0":
        raise SystemExit(f"{path}: not a wasm module")
    pos = 8
    found = []
    while pos < len(data):
        section = data[pos]
        size, pos = leb(data, pos + 1)
        end = pos + size
        if section == 2:
            count, pos = leb(data, pos)
            for _ in range(count):
                module, pos = name(data, pos)
                field, pos = name(data, pos)
                kind = data[pos]
                pos += 1
                if kind == 0:      # function: type index
                    _, pos = leb(data, pos)
                elif kind == 1:    # table: reftype + limits
                    pos = limits(data, pos + 1)
                elif kind == 2:    # memory
                    pos = limits(data, pos)
                elif kind == 3:    # global: valtype + mutability
                    pos += 2
                else:
                    raise SystemExit(f"{path}: unknown import kind {kind}")
                found.append(f"{module}.{field}")
        pos = end
    return sorted(found)


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    actual = imports(sys.argv[1])
    if len(sys.argv) == 4 and sys.argv[3] == "--write":
        with open(sys.argv[2], "w") as f:
            f.write("\n".join(actual) + "\n")
        print(f"wrote {len(actual)} imports to {sys.argv[2]}")
        return 0
    with open(sys.argv[2]) as f:
        expected = sorted(line.strip() for line in f if line.strip() and not line.startswith("#"))
    if actual == expected:
        print(f"imports: {len(actual)}, as listed in {sys.argv[2]}")
        return 0
    for item in sorted(set(actual) - set(expected)):
        print(f"ERROR: the wasm imports {item}, which is not in the list", file=sys.stderr)
    for item in sorted(set(expected) - set(actual)):
        print(f"ERROR: {item} is in the list but the wasm does not import it", file=sys.stderr)
    print("If the change is intended: implement it in web/src/js/emulator/wasm.ts, then\n"
          f"python3 {sys.argv[0]} {sys.argv[1]} {sys.argv[2]} --write", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
