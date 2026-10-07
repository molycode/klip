#!/bin/bash
# Build the release binary dist/klip and the FFmpeg it carries in dist/lib, the ones that ship.
#
#   scripts/build_release.sh
#
# Built in AlmaLinux 9 because nothing at run time can detect a glibc floor that is too high; the gates refuse it here.
# FFmpeg is built from its signed release tarball, LGPL only, and the tarball is kept in dist/ for the release to
# carry beside the package: whoever hands out FFmpeg's libraries hands out their source.

set -u

IMAGE="almalinux:9@sha256:3a3fa7f043b142bc8008c8b308d39b47d2c84008addcd52f9f9a7a82d2a90474"
TOOLSET="gcc-toolset-15"
# Not EL9's own 2.34: its glibc backports _dl_find_object as GLIBC_2.35, which the static libgcc's unwinder calls.
MAX_GLIBC="2.35"

CMAKE_VERSION="3.30.9"
CMAKE_SHA256="9114e33358a9efc93d6ea658805280fc3201b882b944a4d946edd9472fd1eec7"

FFMPEG_VERSION="9.0.2"
FFMPEG_SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
# FFmpeg calls the newest libva its headers offer, and Ubuntu 24.04's libva is 2.20: built against those headers,
# it runs on that libva and every later one.
LIBVA_VERSION="2.20.0"
LIBVA_SHA256="f72bdb4f48dfe71ad01f1cbefe069672a2c949a6abd51cf3c4d4784210badc49"

# Escaped for configure's eval and make's two expansions of it; the linker receives $ORIGIN.
# shellcheck disable=SC2016
FFMPEG_LDSOFLAGS='-Wl,--enable-new-dtags,-rpath,\\\$\$\$\$ORIGIN'

# Every library a shipped file may name as NEEDED besides the FFmpeg it carries: glibc's own, and what every
# distribution with PipeWire 1.0 has. SDL loads everything else by soname at run time.
ALLOWED_NEEDED="libc.so.6 libm.so.6 ld-linux-x86-64.so.2 libdl.so.2 libpthread.so.0 \
libpipewire-0.3.so.0 libsystemd.so.0 libva.so.2 libva-drm.so.2 libdrm.so.2"

# What SDL silently drops when a development package is missing, and what the binary cannot do without.
REQUIRED_SDL_FEATURES="SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC_LIBDECOR SDL_VIDEO_DRIVER_X11 SDL_VIDEO_RENDER_OGL HAVE_DBUS_DBUS_H"

# What --disable-autodetect would drop without a word, and every encoder, muxer and filter Klip asks for that FFmpeg
# lets a configure leave out.
REQUIRED_FFMPEG_FEATURES="HAVE_PTHREADS CONFIG_VAAPI CONFIG_LIBDRM CONFIG_SWSCALE \
CONFIG_H264_VAAPI_ENCODER CONFIG_HEVC_VAAPI_ENCODER CONFIG_AV1_VAAPI_ENCODER CONFIG_AAC_ENCODER \
CONFIG_MP4_MUXER CONFIG_MATROSKA_MUXER CONFIG_WEBM_MUXER \
CONFIG_CROP_FILTER CONFIG_HWMAP_FILTER CONFIG_SCALE_VAAPI_FILTER"

# Each of these on would put a distributed FFmpeg, and Klip with it, under the GPL, LGPLv3 or no licence at all.
FORBIDDEN_FFMPEG_FEATURES="CONFIG_GPL CONFIG_VERSION3 CONFIG_NONFREE"

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
note() { printf '  %s%s%s\n' "$C_DIM" "$*" "$C_OFF"; }

for tool in docker readelf objdump git curl sha256sum; do
	command -v "$tool" >/dev/null 2>&1 || die "$tool is required to build the release"
done
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || die "cannot locate the repository"
cd "$ROOT" || die "cannot enter $ROOT"

VERSION=$(sed -n 's/^project(Klip VERSION \([0-9]*\.[0-9]*\.[0-9]*\) .*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || die "CMakeLists.txt's project() names no version"

# A published release never changes, so its version is built only from a clean checkout of its tag.
if git rev-parse -q --verify "refs/tags/v$VERSION" >/dev/null \
	&& { [ "$(git rev-parse HEAD)" != "$(git rev-parse "v$VERSION^{commit}")" ] || [ -n "$(git status --porcelain)" ]; }; then
	die "Klip $VERSION is released - build it only from a clean checkout of v$VERSION, or raise the version in CMakeLists.txt's project()"
fi

for file in external/tge-core/CMakeLists.txt external/sdl/CMakeLists.txt external/imgui/imgui.h \
	external/json/single_include/nlohmann/json.hpp external/googletest/CMakeLists.txt; do
	[ -f "$file" ] || die "$file is missing - run git submodule update --init --recursive"
done

OUT_DIR="dist"
FFMPEG_TARBALL="$OUT_DIR/ffmpeg-$FFMPEG_VERSION.tar.xz"
mkdir -p "$OUT_DIR" || die "cannot create $OUT_DIR"

printf '\n%sBuilding Klip %s for release%s\n' "$C_B" "$VERSION" "$C_OFF"
note "$IMAGE, $TOOLSET, FFmpeg $FFMPEG_VERSION"

if ! echo "$FFMPEG_SHA256  $FFMPEG_TARBALL" | sha256sum -c --status 2>/dev/null; then
	curl -fsSL -o "$FFMPEG_TARBALL.part" "$FFMPEG_URL" || die "cannot download $FFMPEG_URL"
	mv "$FFMPEG_TARBALL.part" "$FFMPEG_TARBALL" || die "cannot write $FFMPEG_TARBALL"
	echo "$FFMPEG_SHA256  $FFMPEG_TARBALL" | sha256sum -c --status \
		|| die "$FFMPEG_URL does not match the SHA-256 pinned in this script"
fi
ok "FFmpeg source $FFMPEG_TARBALL, SHA-256 as pinned"

WORK=$(mktemp -d) || die "cannot create a temporary directory"
trap 'rm -rf "$WORK"' EXIT

LOG_OUT="/dev/null"
[ -n "${KLIP_VERBOSE:-}" ] && LOG_OUT="/dev/stderr"

# Read-only mounts and the result on stdout as a tar: a root container must not leave root-owned files.
docker run --rm -i \
	-v "$ROOT:/src:ro" \
	-v "$ROOT/$FFMPEG_TARBALL:/ffmpeg.tar.xz:ro" \
	-e LOG_OUT="$LOG_OUT" \
	-e TOOLSET="$TOOLSET" \
	-e CMAKE_VERSION="$CMAKE_VERSION" \
	-e CMAKE_SHA256="$CMAKE_SHA256" \
	-e FFMPEG_LDSOFLAGS="$FFMPEG_LDSOFLAGS" \
	-e LIBVA_VERSION="$LIBVA_VERSION" \
	-e LIBVA_SHA256="$LIBVA_SHA256" \
	"$IMAGE" \
	/bin/bash -c '
		set -e
		{
			dnf -y install dnf-plugins-core
			dnf config-manager --set-enabled crb
			dnf -y install "$TOOLSET-gcc-c++" "$TOOLSET-libstdc++-devel" libstdc++-static make nasm ninja-build \
				pkgconf-pkg-config dbus-daemon bzip2 \
				libX11-devel libXext-devel libXcursor-devel libXi-devel libXfixes-devel libXrandr-devel libXrender-devel \
				libxkbcommon-devel wayland-devel wayland-protocols-devel libdecor-devel \
				mesa-libEGL-devel mesa-libGL-devel dbus-devel \
				pipewire-devel systemd-devel libdrm-devel
			curl -fsSL -o /tmp/cmake.tar.gz \
				"https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz"
			echo "$CMAKE_SHA256  /tmp/cmake.tar.gz" | sha256sum -c -
			tar -C /opt -xzf /tmp/cmake.tar.gz
			curl -fsSL -o /tmp/libva.tar.bz2 \
				"https://github.com/intel/libva/releases/download/$LIBVA_VERSION/libva-$LIBVA_VERSION.tar.bz2"
			echo "$LIBVA_SHA256  /tmp/libva.tar.bz2" | sha256sum -c -
		} > "$LOG_OUT" 2>&1
		source "/opt/rh/$TOOLSET/enable"
		export PATH="/opt/cmake-$CMAKE_VERSION-linux-x86_64/bin:$PATH"

		mkdir /libva && tar -C /libva --strip-components=1 -xjf /tmp/libva.tar.bz2
		cd /libva
		{
			./configure --prefix=/opt/libva --enable-drm --disable-x11 --disable-glx --disable-wayland --disable-docs
			make -j"$(nproc)"
			make install
		} > "$LOG_OUT" 2>&1
		export PKG_CONFIG_PATH=/opt/libva/lib/pkgconfig

		# Autodetection would link whatever the image happens to carry; $ORIGIN lets each library find the
		# others beside it, wherever the package puts them.
		mkdir /ffmpeg && tar -C /ffmpeg --strip-components=1 -xJf /ffmpeg.tar.xz
		cd /ffmpeg
		{
			./configure --prefix=/opt/ffmpeg --disable-gpl --disable-nonfree --enable-shared --disable-static \
				--disable-autodetect --enable-pthreads --enable-vaapi --enable-libdrm \
				--disable-programs --disable-doc --disable-avdevice --disable-network --disable-debug \
				--extra-ldsoflags="$FFMPEG_LDSOFLAGS"
			make -j"$(nproc)"
			make install
		} > "$LOG_OUT" 2>&1

		PKG_CONFIG_PATH=/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH cmake -S /src -B /build -G Ninja \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/linux/gcc.cmake \
			-DKLIP_STATIC_RUNTIME=ON \
			-DKLIP_BUILD_TESTS=ON \
			-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
			"-DCMAKE_INSTALL_RPATH=\$ORIGIN/lib;\$ORIGIN/../lib/klip" \
			-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link,/opt/ffmpeg/lib:/opt/libva/lib > "$LOG_OUT" 2>&1
		cmake --build /build >&2
		LD_LIBRARY_PATH=/opt/ffmpeg/lib:/opt/libva/lib ctest --test-dir /build --output-on-failure >&2
		strip /build/src/app/klip

		mkdir -p /out/lib
		cp /build/src/app/klip /out/klip
		for library in /opt/ffmpeg/lib/lib*.so; do
			soname=$(readelf -d "$library" | sed -n "s/.*(SONAME).*\[\(.*\)\]/\1/p")
			cp -L "$library" "/out/lib/$soname"
			strip --strip-unneeded "/out/lib/$soname"
		done
		cp "$(find /build -name SDL_build_config.h | head -1)" /out/SDL_build_config.h
		cat /ffmpeg/config.h /ffmpeg/config_components.h > /out/ffmpeg_config.h
		sed -n "s/^#define FFMPEG_CONFIGURATION \"\(.*\)\"$/\1/p" /ffmpeg/config.h > /out/ffmpeg-configure.txt
		tar -C /out -cf - .
	' > "$WORK/release.tar" || die "the container build or its tests failed - re-run with KLIP_VERBOSE=1"

mkdir "$WORK/out" || die "cannot create $WORK/out"
tar -C "$WORK/out" -xf "$WORK/release.tar" || die "the container produced no usable result"
[ -s "$WORK/out/klip" ] || die "the container produced no binary"

for feature in $REQUIRED_SDL_FEATURES; do
	grep -qE "^#define $feature( |$)" "$WORK/out/SDL_build_config.h" \
		|| die "SDL was built without $feature - a development package is missing from the container"
done
ok "SDL features: $REQUIRED_SDL_FEATURES"

for feature in $REQUIRED_FFMPEG_FEATURES; do
	grep -qE "^#define $feature 1$" "$WORK/out/ffmpeg_config.h" || die "FFmpeg was built without $feature"
done
for feature in $FORBIDDEN_FFMPEG_FEATURES; do
	grep -qE "^#define $feature 0$" "$WORK/out/ffmpeg_config.h" || die "FFmpeg was built with $feature"
done
ok "FFmpeg is LGPL 2.1+ and has every encoder, muxer and filter Klip uses"

BUNDLED=$(find "$WORK/out/lib" -mindepth 1 -printf '%f ')
[ -n "$BUNDLED" ] || die "the container produced no FFmpeg libraries"

for file in "$WORK/out/klip" "$WORK/out"/lib/*; do
	name=$(basename "$file")

	floor=$(readelf -V "$file" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1)
	[ -n "$floor" ] || die "cannot read the glibc version needs of $name"
	if [ "$(printf '%s\n%s\n' "${floor#GLIBC_}" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
		symbols=$(objdump -T "$file" | grep -F "($floor)" | awk '{print $NF}' | sort -u | tr '\n' ' ')
		die "$name needs $floor (for $symbols), newer than GLIBC_$MAX_GLIBC - the build host was too new"
	fi

	for needed in $(readelf -d "$file" | grep NEEDED | sed -E 's/.*\[(.*)\]/\1/'); do
		case " $ALLOWED_NEEDED $BUNDLED " in
			*" $needed "*) ;;
			*) die "$name needs $needed, which is neither on the allowlist nor carried in lib/" ;;
		esac
	done

	runpath=$(readelf -d "$file" | sed -n 's/.*(RUNPATH).*\[\(.*\)\]/\1/p')
	expected="\$ORIGIN"
	[ "$name" = klip ] && expected="\$ORIGIN/lib:\$ORIGIN/../lib/klip"
	[ "$runpath" = "$expected" ] || die "$name has runpath '$runpath' instead of '$expected'"
done
ok "glibc floor at most GLIBC_$MAX_GLIBC, runpaths \$ORIGIN only, for klip and $BUNDLED"
ok "klip needs only: $(readelf -d "$WORK/out/klip" | grep NEEDED | sed -E 's/.*\[(.*)\]/\1/' | tr '\n' ' ')"

rm -rf "${OUT_DIR:?}/klip" "${OUT_DIR:?}/lib" || die "cannot clear the previous build from $OUT_DIR"
mv "$WORK/out/klip" "$OUT_DIR/klip" || die "cannot write $OUT_DIR/klip"
mv "$WORK/out/lib" "$OUT_DIR/lib" || die "cannot write $OUT_DIR/lib"
mv "$WORK/out/ffmpeg-configure.txt" "$OUT_DIR/ffmpeg-configure.txt" || die "cannot write $OUT_DIR/ffmpeg-configure.txt"
chmod 755 "$OUT_DIR/klip"
chmod 644 "$OUT_DIR"/lib/*

"$OUT_DIR/klip" --version > /dev/null 2>&1 || die "$OUT_DIR/klip does not start here - its libraries do not resolve"
ok "$OUT_DIR/klip  $(( $(stat -c%s "$OUT_DIR/klip") / 1024 )) KiB, starts here"
ok "$OUT_DIR/lib  $(( $(du -sk "$OUT_DIR/lib" | cut -f1) )) KiB"
