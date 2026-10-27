#!/usr/bin/env sh
# Register Balls in the desktop application menu and put a launcher on the desktop.
# Uses absolute paths to this checkout, so run it again after moving the directory.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/dist/linux-sdl/ColorBlastBalls"

if [ ! -x "$binary" ]; then
    echo "Not built yet: $binary" >&2
    echo "Run $root/build-linux-sdl.sh first." >&2
    exit 1
fi

apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
desktop_dir=$(xdg-user-dir DESKTOP 2>/dev/null || true)
if [ -z "$desktop_dir" ] || [ "$desktop_dir" = "$HOME" ]; then
    desktop_dir="$HOME/Desktop"
fi

entry_name=color-blast-balls.desktop
menu_entry="$apps_dir/$entry_name"
mkdir -p "$apps_dir"

write_entry() {
    cat > "$1" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Balls
GenericName=Arcade game
Comment=Balls
Exec=$root/run-linux-sdl.sh %U
TryExec=$binary
Path=$root
Icon=$root/data/packaging/linux/color-blast-balls.svg
Terminal=false
Categories=Game;ArcadeGame;
Keywords=balls;arcade;color;
StartupNotify=true
EOF
    chmod +x "$1"
}

write_entry "$menu_entry"
update-desktop-database "$apps_dir" >/dev/null 2>&1 || true

if [ -d "$desktop_dir" ]; then
    write_entry "$desktop_dir/$entry_name"
    # GNOME and KDE only launch desktop-file icons that are marked trusted and executable.
    gio set "$desktop_dir/$entry_name" metadata::trusted true >/dev/null 2>&1 || true
    echo "Desktop shortcut: $desktop_dir/$entry_name"
fi

echo "Menu entry: $menu_entry"
