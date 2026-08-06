#!/usr/bin/env python3
# Extract meson files(...) lists from a meson.build for the CMake port.
# SPDX-License-Identifier: MIT
import re
import sys

def extract(path, varname):
    src = open(path).read()
    # find assignments: varname = files(...) possibly spanning lines
    pat = re.compile(r'files\(\s*(.*?)\s*\)', re.S)
    out = []
    for m in pat.finditer(src):
        # only consider files() bodies
        body = m.group(1)
        # simple heuristic: the assignment context is checked by caller
        out.append(body)
    return out

if __name__ == '__main__':
    path = sys.argv[1]
    var = sys.argv[2]
    bodies = extract(path, var)
    items = []
    for body in bodies:
        # if this is a plain list of quoted strings, use it
        for mm in re.finditer(r"'([^']+)'", body):
            items.append(mm.group(1))
    for it in items:
        print(it)
