#!/usr/bin/env python3
"""
Bundle traycon source files into a single stb-style header-only library.

Output: traycon.h (in the repo root)

Usage in your project:

    #include "traycon.h"            // declarations only

In exactly ONE .c file:

    #define TRAYCON_IMPLEMENTATION
    #include "traycon.h"            // pulls in the implementation
"""

import os
import re
import sys
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(SCRIPT_DIR, "src")
OUTPUT = os.path.join(SCRIPT_DIR, "traycon.h")

HEADER = os.path.join(SRC_DIR, "traycon.h")

# (filename, platform_guard)
# Files that already contain their own #ifdef guard use None;
# files without one get wrapped with the given guard here.
IMPL_FILES = [
    (os.path.join(SRC_DIR, "traycon_linux.c"),  "__linux__"),
    (os.path.join(SRC_DIR, "traycon_win32.c"),  None),
    (os.path.join(SRC_DIR, "traycon_macos.m"),  None),
]


def read(path):
    with open(path, "r") as f:
        return f.read()


def strip_local_includes(src):
    """Remove #include "traycon.h" (the local header) from implementation files."""
    return re.sub(r'^\s*#\s*include\s+"traycon\.h"\s*\n', "", src, flags=re.MULTILINE)


def strip_header_guard_close(header_src):
    """Remove the closing #endif of the include guard and any trailing whitespace."""
    # Remove the last #endif /* TRAYCON_H */ (and optional comment)
    return re.sub(
        r'\n*#endif\s*/\*\s*TRAYCON_H\s*\*/\s*$', "", header_src
    )


def build():
    header_src = read(HEADER)
    header_body = strip_header_guard_close(header_src)

    impl_parts = []
    for path, guard in IMPL_FILES:
        src = read(path)
        src = strip_local_includes(src)
        name = os.path.basename(path)
        body = src.strip()
        if guard:
            body = f"#ifdef {guard}\n{body}\n#endif /* {guard} */"
        impl_parts.append(
            f"/* ====== begin {name} ====== */\n"
            f"{body}\n"
            f"/* ====== end {name} ====== */"
        )

    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    impl_block = "\n\n".join(impl_parts)

    output = f"""\
{header_body.rstrip()}

/* ---------------------------------------------------------------------- */
/*  IMPLEMENTATION                                                        */
/*                                                                        */
/*  In exactly ONE C file, define TRAYCON_IMPLEMENTATION before including */
/*  this header:                                                          */
/*                                                                        */
/*      #define TRAYCON_IMPLEMENTATION                                    */
/*      #include "traycon.h"                                              */
/* ---------------------------------------------------------------------- */

#ifdef TRAYCON_IMPLEMENTATION
#ifndef TRAYCON_IMPLEMENTATION_GUARD
#define TRAYCON_IMPLEMENTATION_GUARD

{impl_block}

#endif /* TRAYCON_IMPLEMENTATION_GUARD */
#endif /* TRAYCON_IMPLEMENTATION */

#endif /* TRAYCON_H */
"""

    with open(OUTPUT, "w") as f:
        f.write(output)

    print(f"Bundled -> {os.path.relpath(OUTPUT, SCRIPT_DIR)}")
    print(f"  header:  {os.path.relpath(HEADER, SCRIPT_DIR)}")
    for p, _ in IMPL_FILES:
        print(f"  impl:    {os.path.relpath(p, SCRIPT_DIR)}")


if __name__ == "__main__":
    build()
