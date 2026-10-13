#!/bin/sh
# Dev install for the current user: desktop file pointing at the build tree, and the icon.
set -e
share="${XDG_DATA_HOME:-$HOME/.local/share}"
desktop="$share/applications/$(basename "$1")"
mkdir -p "$share/applications"
sed "s|^Exec=.*|Exec=$3|" "$1" > "$desktop"
install -Dm644 "$2" "$share/icons/hicolor/scalable/apps/$(basename "$2")"
update-desktop-database -q "$share/applications" 2>/dev/null || true
gtk-update-icon-cache -q -t "$share/icons/hicolor" 2>/dev/null || true
echo "Installed $(basename "$1") and its icon in $share"
