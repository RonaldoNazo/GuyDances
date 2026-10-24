#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:?usage: package-linux.sh VERSION}"
OUT="$PWD/release"
mkdir -p "$OUT"

[ -x dist/guydances ] || { echo "build first: ./build-linux.sh" >&2; exit 1; }

# Portable tarball -----------------------------------------------------------
ROOT="$(mktemp -d)"
STAGE="$ROOT/guydances-${VERSION}-linux-x86_64"
mkdir -p "$STAGE"
cp dist/guydances "$STAGE/"
cp assets/icon.png "$STAGE/guydances.png"
cp packaging/linux/guydances.desktop.in "$STAGE/"
cp packaging/linux/install.sh "$STAGE/"
cp README.md "$STAGE/"
tar -C "$ROOT" -czf "$OUT/guydances-${VERSION}-linux-x86_64.tar.gz" "$(basename "$STAGE")"
cp dist/guydances "$OUT/guydances-${VERSION}-linux-x86_64"
rm -rf "$ROOT"

# Debian package -------------------------------------------------------------
DEB="$PWD/packaging/deb"
rm -rf "$DEB"
mkdir -p "$DEB/DEBIAN" \
         "$DEB/usr/bin" \
         "$DEB/usr/share/applications" \
         "$DEB/usr/share/icons/hicolor/256x256/apps"

install -m 755 dist/guydances "$DEB/usr/bin/guydances"
install -m 644 assets/icon.png "$DEB/usr/share/icons/hicolor/256x256/apps/guydances.png"
sed 's|@BIN@|/usr/bin/guydances|' packaging/linux/guydances.desktop.in \
    > "$DEB/usr/share/applications/guydances.desktop"
chmod 644 "$DEB/usr/share/applications/guydances.desktop"

SIZE_KB="$(du -sk "$DEB/usr" | cut -f1)"
cat > "$DEB/DEBIAN/control" <<EOF
Package: guydances
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Depends: libc6, libstdc++6, libgcc-s1, libgl1, libx11-6, libpulse0
Installed-Size: ${SIZE_KB}
Maintainer: Ronaldo Nazo <aldo_nazo@yahoo.com>
Homepage: https://github.com/RonaldoNazo/GuyDances
Description: Pixel dancer that appears when music plays
 A tiny desktop companion that dances while a media app is playing sound,
 and disappears when the music stops. Drag him anywhere, right-click for
 pause, stop and quit.
EOF

dpkg-deb --build --root-owner-group "$DEB" \
    "$OUT/guydances_${VERSION}_amd64.deb" >/dev/null
rm -rf "$DEB"

echo "Linux packages:"
ls -lh "$OUT"
