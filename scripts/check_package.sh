#!/bin/bash
# Check a package in clean containers of the distros Klip supports: it installs, every library and symbol it needs
# resolves, and it uninstalls cleanly everywhere, and on two of them its window starts under Xvfb.
#
#   scripts/check_package.sh [dist/klip-<version>-x86_64.tar.xz]
#
# Needs the network for the images and their package mirrors. A container has no portal and no graphics card, so
# recording is not checked here: scripts/smoke_test.py does that against an installed package.

set -u

INSTALL_IMAGES="ubuntu:24.04 ubuntu:26.04 debian:13 fedora:latest archlinux:latest almalinux:9"
GUI_IMAGES="ubuntu:24.04 fedora:latest"

# The libraries Klip links that every desktop with PipeWire 1.0 has and a slim image lacks.
APT_RUNTIME="libpipewire-0.3-0 libva2 libva-drm2 libdrm2 libsystemd0"
DNF_RUNTIME="pipewire-libs libva libdrm systemd-libs"
PACMAN_RUNTIME="libpipewire libva libdrm systemd-libs"

# Xvfb, Mesa, the X libraries SDL loads by soname, and a session bus to own Klip's name on. No portal: without one
# Klip must still open its window, to say what is missing.
APT_GUI="xvfb libgl1 libgl1-mesa-dri libegl1 libx11-6 libxext6 libxcursor1 libxi6 libxfixes3 libxrandr2 libxrender1 \
libxkbcommon0 dbus-daemon"
DNF_GUI="xorg-x11-server-Xvfb mesa-dri-drivers mesa-libGL mesa-libEGL libX11 libXext libXcursor libXi libXfixes libXrandr \
libXrender libxkbcommon dbus-daemon"

apt_setup()    { echo "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $*"; }
dnf_setup()    { echo "dnf -y install $*"; }
pacman_setup() { echo "pacman -Sy --noconfirm --needed $*"; }

if [ -t 1 ]; then
	C_DIM=$(printf '\033[2m'); C_B=$(printf '\033[1m'); C_OFF=$(printf '\033[0m')
	C_OK=$(printf '\033[32m'); C_ERR=$(printf '\033[31m')
else
	C_DIM=""; C_B=""; C_OFF=""; C_OK=""; C_ERR=""
fi
case "${LANG:-}${LC_ALL:-}" in
	*UTF-8*|*utf8*|*UTF8*) G_OK="✓"; G_ERR="✗" ;;
	*) G_OK="-"; G_ERR="x" ;;
esac

die()  { printf '\n%s%s %s%s\n' "$C_ERR" "$G_ERR" "$*" "$C_OFF" >&2; exit 1; }
ok()   { printf '  %s%s%s %s\n' "$C_OK" "$G_OK" "$C_OFF" "$*"; }
bad()  { printf '  %s%s%s %s\n' "$C_ERR" "$G_ERR" "$C_OFF" "$*"; }
note() { printf '  %s%s%s\n' "$C_DIM" "$*" "$C_OFF"; }

command -v docker >/dev/null 2>&1 || die "docker is required to check the package"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || die "cannot locate the repository"
cd "$ROOT" || die "cannot enter $ROOT"

TARBALL="${1:-$(ls -t dist/klip-*-x86_64.tar.xz 2>/dev/null | head -n 1)}"
[ -n "$TARBALL" ] && [ -f "$TARBALL" ] || die "no package to check - run scripts/make_package.sh first"

WORK=$(mktemp -d) || die "cannot create a temporary directory"
trap 'rm -rf "$WORK"' EXIT

# Unpacked here: a slim image may have no xz.
mkdir "$WORK/package" "$WORK/checks" || die "cannot prepare $WORK"
tar -C "$WORK/package" -xJf "$TARBALL" || die "cannot unpack $TARBALL"
PACKAGE=$(find "$WORK/package" -mindepth 1 -maxdepth 1 -type d | head -n 1)
[ -n "$PACKAGE" ] && [ -f "$PACKAGE/install.sh" ] || die "$TARBALL holds no install.sh"

cat > "$WORK/checks/install.sh" <<'EOF'
set -u
fail() { echo "FAIL $1"; [ -f /tmp/out ] && tail -n 8 /tmp/out; exit 1; }
sh -c "$SETUP" > /tmp/out 2>&1 || fail "installing the runtime libraries"
cp -R /pkg /tmp/pkg || fail "copying the package"
sh /tmp/pkg/install.sh > /tmp/out 2>&1 || fail "install.sh"
sh /tmp/pkg/install.sh > /tmp/out 2>&1 || fail "install.sh over an existing install"
version=$("$HOME/.local/bin/klip" --version 2> /tmp/out) || fail "klip --version"
# -r resolves every symbol, so a PipeWire older than the one built against fails here rather than mid-recording.
for file in "$HOME/.local/bin/klip" "$HOME"/.local/lib/klip/*; do
	ldd -r "$file" > /tmp/out 2>&1 || fail "ldd -r $file"
	! grep -E "not found|undefined symbol" /tmp/out > /dev/null || fail "$file does not resolve here"
done
# Settings and logs belong to the user, and must outlive the program.
mkdir -p "$HOME/.config/klip" "$HOME/.local/state/klip/logs" || fail "seeding settings and logs"
: > "$HOME/.config/klip/config.json" && : > "$HOME/.local/state/klip/logs/klip_seed.log" || fail "seeding settings and logs"
for uninstaller in "$HOME/.local/share/klip/uninstall.sh" /tmp/pkg/uninstall.sh; do
	[ -f "$HOME/.local/bin/klip" ] || sh /tmp/pkg/install.sh > /tmp/out 2>&1 || fail "install.sh, again"
	sh "$uninstaller" > /tmp/out 2>&1 || fail "$uninstaller"
	grep -q "is uninstalled" /tmp/out || fail "$uninstaller found nothing to remove"
	for leftover in "$HOME/.local/bin/klip" "$HOME/.local/lib/klip" "$HOME/.local/share/applications/klip.desktop" \
		"$HOME/.local/share/icons/hicolor/scalable/apps/klip.svg" "$HOME/.local/share/klip"; do
		[ ! -e "$leftover" ] || fail "$uninstaller left $leftover"
	done
	[ -f "$HOME/.config/klip/config.json" ] && [ -f "$HOME/.local/state/klip/logs/klip_seed.log" ] \
		|| fail "$uninstaller removed the settings or the logs"
done
echo "PASS $version installs, resolves every library and symbol, uninstalls cleanly with either uninstaller"
EOF

cat > "$WORK/checks/gui.sh" <<'EOF'
set -u
fail() { echo "FAIL $1"; [ -f /tmp/out ] && tail -n 8 /tmp/out; exit 1; }
sh -c "$SETUP" > /tmp/out 2>&1 || fail "installing Xvfb, the X and GL libraries and a session bus"
cp -R /pkg /tmp/pkg || fail "copying the package"
sh /tmp/pkg/install.sh > /tmp/out 2>&1 || fail "install.sh"
Xvfb :99 -screen 0 1280x800x24 > /tmp/xvfb.out 2>&1 &
waited=0
while [ ! -S /tmp/.X11-unix/X99 ] && [ "$waited" -lt 40 ]; do
	sleep 0.25
	waited=$((waited + 1))
done
[ -S /tmp/.X11-unix/X99 ] || { cp /tmp/xvfb.out /tmp/out; fail "Xvfb did not start within 10 s"; }
status=0
DISPLAY=:99 dbus-run-session -- timeout -k 5 5 "$HOME/.local/bin/klip" > /tmp/out 2>&1 || status=$?
[ "$status" -eq 124 ] || fail "klip ended with status $status instead of running until the 5 s timeout"
log=$(ls -t "$HOME"/.local/state/klip/logs/klip_*.log 2>/dev/null | head -n 1)
[ -n "$log" ] || fail "klip wrote no log"
cp "$log" /tmp/out
display=$(grep -o "video driver 'x11', renderer '[^']*'" "$log") || fail "the log names no x11 video driver and renderer"
echo "PASS window starts under Xvfb with no portal: $display"
EOF

# Prints the check's one-line outcome and returns whether it passed.
run_check() {
	local image=$1 script=$2 setup=$3 output result
	output=$(docker run --rm -v "$PACKAGE:/pkg:ro" -v "$WORK/checks:/checks:ro" -e SETUP="$setup" "$image" sh "/checks/$script" 2>&1)
	result=$(printf '%s\n' "$output" | grep -E '^(PASS|FAIL) ' | tail -n 1)

	if [[ "$result" == PASS* ]]; then
		ok "$image  ${result#PASS }"
	else
		bad "$image  ${result#FAIL }"
		printf '%s\n' "$output" | sed -n '/^FAIL /,$p' | tail -n +2 | sed 's/^/      /'
		[ -n "$result" ] || printf '%s\n' "$output" | tail -n 5 | sed 's/^/      /'
		false
	fi
}

printf '\n%sChecking %s%s\n' "$C_B" "$TARBALL" "$C_OFF"
note "$INSTALL_IMAGES; window on $GUI_IMAGES"

failures=0

for image in $INSTALL_IMAGES; do
	case "$image" in
		ubuntu:*|debian:*) setup=$(apt_setup "$APT_RUNTIME") ;;
		archlinux:*) setup=$(pacman_setup "$PACMAN_RUNTIME") ;;
		*) setup=$(dnf_setup "$DNF_RUNTIME") ;;
	esac

	run_check "$image" install.sh "$setup" || failures=$((failures + 1))
done

for image in $GUI_IMAGES; do
	case "$image" in
		ubuntu:*|debian:*) setup=$(apt_setup "$APT_RUNTIME $APT_GUI") ;;
		*) setup=$(dnf_setup "$DNF_RUNTIME $DNF_GUI") ;;
	esac

	run_check "$image" gui.sh "$setup" || failures=$((failures + 1))
done

[ "$failures" -eq 0 ] || die "$failures check(s) failed"
ok "every check passed"
