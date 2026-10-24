#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

missing=0
for header in \
    X11/extensions/Xrandr.h \
    X11/extensions/Xinerama.h \
    X11/Xcursor/Xcursor.h \
    X11/extensions/XInput2.h \
    X11/extensions/shape.h \
    X11/extensions/Xrender.h \
    X11/extensions/Xfixes.h; do
    [ -f "/usr/include/$header" ] || missing=1
done
if [ "$missing" = "1" ]; then
    echo "Missing X11 development headers. Install them once with:"
    echo
    echo "  sudo apt install -y cmake libpulse-dev libx11-dev libgl-dev \\"
    echo "    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \\"
    echo "    libxext-dev libxrender-dev libxfixes-dev"
    echo
    exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
mkdir -p dist
cp build/guydances dist/guydances
strip dist/guydances
ls -lh dist/guydances
