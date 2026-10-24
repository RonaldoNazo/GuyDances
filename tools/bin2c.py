#!/usr/bin/env python3
"""Embed a binary blob as a C header."""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("#pragma once\n")
    f.write(f"static const unsigned int kDanceSize = {len(data)};\n")
    f.write("static const unsigned char kDanceData[] = {\n")
    for i in range(0, len(data), 20):
        chunk = ",".join(f"0x{b:02x}" for b in data[i : i + 20])
        f.write(chunk + ",\n")
    f.write("};\n")
