#!/usr/bin/env python3
"""Embed Antidox's compiled display helpers into its framework preload."""

import sys
from pathlib import Path

classes, output = map(Path, sys.argv[1:])
paths = sorted(classes.rglob("*.class"), key=lambda path: (path.name.count("$"), str(path)))
lines = ["#pragma once", "#include <cstddef>"]
records = []
for index, path in enumerate(paths):
    data = path.read_bytes()
    symbol = f"antidoxClass{index}"
    lines.append(f"static const unsigned char {symbol}[] = {{")
    for offset in range(0, len(data), 24):
        lines.append("    " + ",".join(str(value) for value in data[offset:offset + 24]) + ",")
    lines.append("};")
    name = path.relative_to(classes).with_suffix("").as_posix()
    records.append(f'    {{"{name}", {symbol}, sizeof({symbol})}},')
lines.extend([
    "struct AntidoxClass { const char *name; const unsigned char *data; std::size_t length; };",
    "static const AntidoxClass antidoxClasses[] = {",
    *records,
    "};",
])
output.write_text("\n".join(lines) + "\n")
