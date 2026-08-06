#!/usr/bin/env python3
# Extract a specific meson files(...) list following an assignment context.
# Usage: extract_assignment.py <meson.build> <context-regex> [limit]
# Prints quoted strings from the first files(...) following a line matching context-regex.
# SPDX-License-Identifier: MIT
import re
import sys

path, ctx = sys.argv[1], sys.argv[2]
src = open(path).read()
lines = src.split('\n')
out = []
in_files = False
depth = 0
for i, line in enumerate(lines):
    if not in_files and re.search(ctx, line):
        # find files( starting on this or later lines
        j = i
        while j < len(lines):
            m = re.search(r'files\(', lines[j])
            if m:
                # collect from after files(
                buf = lines[j][m.end():]
                depth = buf.count('(') - buf.count(')')
                while depth > 0 or (buf.strip() == '' and j < len(lines)):
                    j += 1
                    buf += lines[j]
                    depth = buf.count('(') - buf.count(')')
                for mm in re.finditer(r"'([^']+)'", buf):
                    out.append(mm.group(1))
                in_files = True
                break
            j += 1
    if in_files:
        break
for it in out:
    print(it)
