#!/bin/sh
# Removes what install.sh put in place; settings and logs are kept, and recordings are never touched.
#
#   ./uninstall.sh      (from the package, or as ~/.local/share/klip/uninstall.sh)

set -eu

die() { printf 'uninstall.sh: %s\n' "$*" >&2; exit 1; }

# The XDG base directory spec ignores a relative value.
xdg_dir() {
	case "$1" in
		/*) printf '%s' "$1" ;;
		*) printf '%s' "$HOME/$2" ;;
	esac
}

[ -n "${HOME:-}" ] || die "HOME is not set"

DATA_DIR=$(xdg_dir "${XDG_DATA_HOME:-}" .local/share)
CONFIG_DIR=$(xdg_dir "${XDG_CONFIG_HOME:-}" .config)/klip
STATE_DIR=$(xdg_dir "${XDG_STATE_HOME:-}" .local/state)/klip
LIB_DIR="$HOME/.local/lib/klip"
UNINSTALL_DIR="$DATA_DIR/klip"
isFound=false

for file in "$HOME/.local/bin/klip" "$DATA_DIR/applications/klip.desktop" \
	"$DATA_DIR/icons/hicolor/scalable/apps/klip.svg" "$UNINSTALL_DIR/uninstall.sh"; do
	if [ -e "$file" ]; then
		rm -f "$file"
		isFound=true
	fi
done

if [ -d "$LIB_DIR" ]; then
	rm -rf "$LIB_DIR"
	isFound=true
fi

rmdir "$UNINSTALL_DIR" 2>/dev/null || true

if [ "$isFound" = true ]; then
	echo "Klip is uninstalled. Its settings in $CONFIG_DIR and logs in $STATE_DIR are kept."
else
	echo "Klip is not installed for this user: there was nothing to remove."
fi
