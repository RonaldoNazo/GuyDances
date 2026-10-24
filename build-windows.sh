#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j"$(nproc)"
mkdir -p dist
cp build-win/guydances.exe dist/guydances.exe
x86_64-w64-mingw32-strip dist/guydances.exe
ls -lh dist/guydances.exe
