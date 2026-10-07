#!/bin/sh
# Installs Klip for the current user under ~/.local, with a menu entry, an icon and an uninstaller.
#
#   ./install.sh        (from the folder the package unpacked to)

set -eu

die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

# The XDG base directory spec ignores a relative value.
xdg_dir() {
	case "$1" in
		/*) printf '%s' "$1" ;;
		*) printf '%s' "$HOME/$2" ;;
	esac
}

[ -n "${HOME:-}" ] || die "HOME is not set"

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
BIN_DIR="$HOME/.local/bin"
# Fixed beside BIN_DIR: the binary finds its FFmpeg at $ORIGIN/../lib/klip.
LIB_DIR="$HOME/.local/lib/klip"
DATA_DIR=$(xdg_dir "${XDG_DATA_HOME:-}" .local/share)
APPS_DIR="$DATA_DIR/applications"
ICONS_DIR="$DATA_DIR/icons/hicolor"
BINARY="$BIN_DIR/klip"
DESKTOP="$APPS_DIR/klip.desktop"
ICON="$ICONS_DIR/scalable/apps/klip.svg"
UNINSTALL="$DATA_DIR/klip/uninstall.sh"

for file in klip klip.desktop klip.svg uninstall.sh; do
	[ -f "$HERE/$file" ] || die "$file is missing beside install.sh - unpack the whole package"
done

[ -n "$(ls "$HERE/lib" 2>/dev/null)" ] || die "lib/ is missing beside install.sh - unpack the whole package"

# These need escaping in a desktop entry's Exec.
case "$BINARY" in
	*'"'* | *'`'* | *'$'* | *\\* | *'%'*)
		die "cannot install under $HOME: the path holds a quote, backquote, dollar, backslash or percent sign" ;;
esac

VERSION=$("$HERE/klip" --version 2>&1) \
	|| die "klip does not start here: $VERSION"

mkdir -p "$BIN_DIR" "$LIB_DIR" "$APPS_DIR" "$ICONS_DIR/scalable/apps" "$(dirname "$UNINSTALL")"

# First, so an install that fails part-way can still be undone.
cp "$HERE/uninstall.sh" "$UNINSTALL"
chmod 755 "$UNINSTALL"

# Through a rename: copying over a file a running Klip has mapped fails with "Text file busy".
for library in "$HERE"/lib/*; do
	name=$(basename "$library")
	cp "$library" "$LIB_DIR/$name.new"
	chmod 644 "$LIB_DIR/$name.new"
	mv -f "$LIB_DIR/$name.new" "$LIB_DIR/$name"
done

# An older package's FFmpeg had other sonames, which nothing loads any more.
for library in "$LIB_DIR"/*; do
	[ -e "$HERE/lib/$(basename "$library")" ] || rm -f "$library"
done

cp "$HERE/klip" "$BINARY.new"
chmod 755 "$BINARY.new"
mv -f "$BINARY.new" "$BINARY"

while IFS= read -r line; do
	case "$line" in
		Exec=*) printf 'Exec="%s"\n' "$BINARY" ;;
		TryExec=*) printf 'TryExec=%s\n' "$BINARY" ;;
		*) printf '%s\n' "$line" ;;
	esac
done < "$HERE/klip.desktop" > "$DESKTOP"
chmod 644 "$DESKTOP"

cp "$HERE/klip.svg" "$ICON"
chmod 644 "$ICON"

# GTK rescans an icon folder whose time changed; there is no cache to update in a user's own theme folder.
touch "$ICONS_DIR" || true

"$BINARY" --version > /dev/null 2>&1 || die "the installed $BINARY does not start - its libraries in $LIB_DIR do not resolve"

echo "$VERSION is installed:"
echo "  $BINARY"
echo "  $LIB_DIR/"
echo "  $DESKTOP"
echo "  $ICON"

case ":${PATH:-}:" in
	*":$BIN_DIR:"*) echo "Start it from the applications menu, or run: klip" ;;
	*) echo "Start it from the applications menu, or run: $BINARY" ;;
esac

echo "To uninstall it: $HERE/uninstall.sh (or $UNINSTALL once this folder is gone)"
