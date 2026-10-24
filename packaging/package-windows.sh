#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?usage: package-windows.sh VERSION}"
OUT="$PWD/release"
mkdir -p "$OUT"

[ -f dist/guydances.exe ] || { echo "build first: ./build-windows.sh" >&2; exit 1; }

ROOT="$(mktemp -d)"
STAGE="$ROOT/guydances-${VERSION}-windows-x64"
mkdir -p "$STAGE"
cp dist/guydances.exe "$STAGE/"
cp packaging/windows/README.txt "$STAGE/"

(cd "$ROOT" && zip -qr "$OUT/guydances-${VERSION}-windows-x64.zip" "$(basename "$STAGE")")
cp dist/guydances.exe "$OUT/guydances-${VERSION}-windows-x64.exe"
rm -rf "$ROOT"

echo "Windows packages:"
ls -lh "$OUT"
