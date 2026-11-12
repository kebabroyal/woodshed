#!/bin/sh
# SPDX-FileCopyrightText: 2026 kebabroyal
# SPDX-License-Identifier: GPL-3.0-or-later
# Dev build in the app menu as "Woodshed (dev)": desktop file and icon for the current user.
set -e
share="${XDG_DATA_HOME:-$HOME/.local/share}"
install -Dm644 "$1" "$share/applications/$(basename "$1")"
install -Dm644 "$2" "$share/icons/hicolor/scalable/apps/$(basename "$2")"
update-desktop-database -q "$share/applications" 2>/dev/null || true
gtk-update-icon-cache -q -t "$share/icons/hicolor" 2>/dev/null || true
echo "Installed $(basename "$1") and its icon in $share"
