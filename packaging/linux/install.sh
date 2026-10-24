#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
mkdir -p "$BIN" "$APPS" "$ICONS"

install -m 755 guydances "$BIN/guydances"
install -m 644 guydances.png "$ICONS/guydances.png"
sed "s|@BIN@|$BIN/guydances|" guydances.desktop.in > "$APPS/guydances.desktop"
chmod +x "$APPS/guydances.desktop"

if [ "${1:-}" = "--autostart" ]; then
    mkdir -p "$HOME/.config/autostart"
    cat > "$HOME/.config/autostart/guydances.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=GuyDances
Comment=Dances when music is playing
Exec=$BIN/guydances
Icon=guydances
Terminal=false
X-GNOME-Autostart-enabled=true
DESKTOP
    chmod +x "$HOME/.config/autostart/guydances.desktop"
    echo "Autostart enabled."
fi

echo "Installed: $BIN/guydances"
echo "Launch it from the app grid, or run: guydances"
