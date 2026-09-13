#!/usr/bin/env bash
#
# Builds an LGPL FFmpeg for KLIP_FFMPEG_ROOT to point at. The build resolves FFmpeg through pkg-config
# without it, so nothing here is required.
#
# No --enable-gpl: a GPL libavcodec would make any distributed Klip binary GPL. VAAPI does not need it.
#
# Override TOOLS_DIR to install somewhere other than ~/.local/opt.

set -euo pipefail

FFMPEG_TAG="${FFMPEG_TAG:-n8.1.2}"
TOOLS_DIR="${TOOLS_DIR:-${HOME}/.local/opt}"
PREFIX="${PREFIX:-${TOOLS_DIR}/ffmpeg-${FFMPEG_TAG#n}-lgpl}"
SRC_DIR="${SRC_DIR:-${TOOLS_DIR}/src/ffmpeg}"
JOBS="${JOBS:-$(nproc)}"

echo "tag    ${FFMPEG_TAG}"
echo "source ${SRC_DIR}"
echo "prefix ${PREFIX}"
echo "jobs   ${JOBS}"

for tool in nasm pkg-config make git; do
	command -v "${tool}" >/dev/null || { echo "missing build tool: ${tool}" >&2; exit 1; }
done

for mod in libva libva-drm libdrm; do
	pkg-config --exists "${mod}" || { echo "missing dev package for: ${mod}" >&2; exit 1; }
done

if [ -d "${SRC_DIR}/.git" ]; then
	git -C "${SRC_DIR}" fetch --depth 1 origin "refs/tags/${FFMPEG_TAG}:refs/tags/${FFMPEG_TAG}"
	git -C "${SRC_DIR}" checkout -q "${FFMPEG_TAG}"
else
	mkdir -p "$(dirname "${SRC_DIR}")"
	git clone --depth 1 --branch "${FFMPEG_TAG}" https://git.ffmpeg.org/ffmpeg.git "${SRC_DIR}"
fi

cd "${SRC_DIR}"

# Out-of-tree, so switching tags never links objects from the previous one.
# --enable-rpath: DT_RUNPATH is not inherited by transitive loads, so libavcodec would miss libswresample.
BUILD_DIR="${SRC_DIR}/build-lgpl"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

"${SRC_DIR}/configure" \
	--prefix="${PREFIX}" \
	--disable-gpl \
	--disable-nonfree \
	--enable-shared \
	--disable-static \
	--enable-vaapi \
	--enable-rpath \
	--enable-libdrm \
	--disable-programs \
	--disable-doc \
	--disable-avdevice \
	--disable-network \
	--disable-debug

make -j"${JOBS}"
make install

echo
echo "installed to ${PREFIX}"
echo "point CMake at it with KLIP_FFMPEG_ROOT=${PREFIX} in CMakeUserPresets.json"
