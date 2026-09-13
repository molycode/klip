# pkg-config is the mechanism, because that is what a user compiling the source package has.
# KLIP_FFMPEG_ROOT only puts a pinned build ahead of the system one. The floor is FFmpeg 6.1.

find_package(PkgConfig REQUIRED)

if(NOT KLIP_FFMPEG_ROOT AND DEFINED ENV{KLIP_FFMPEG_ROOT})
	set(KLIP_FFMPEG_ROOT "$ENV{KLIP_FFMPEG_ROOT}" CACHE PATH "Pinned FFmpeg installation root")
endif()

if(KLIP_FFMPEG_ROOT)
	if(NOT EXISTS "${KLIP_FFMPEG_ROOT}/lib/pkgconfig")
		message(FATAL_ERROR
			"KLIP_FFMPEG_ROOT is ${KLIP_FFMPEG_ROOT}, which has no lib/pkgconfig. "
			"Build it with scripts/build_ffmpeg.sh, or unset KLIP_FFMPEG_ROOT to use the system FFmpeg.")
	endif()

	set(ENV{PKG_CONFIG_PATH} "${KLIP_FFMPEG_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	message(STATUS "[Klip] FFmpeg: pinned build at ${KLIP_FFMPEG_ROOT}")
else()
	message(STATUS "[Klip] FFmpeg: system packages via pkg-config")
endif()

pkg_check_modules(KlipFFmpeg IMPORTED_TARGET
	libavcodec>=60
	libavfilter>=9
	libavformat>=60
	libavutil>=58
	libswscale>=7
)

if(NOT KlipFFmpeg_FOUND)
	message(FATAL_ERROR
		"FFmpeg 6.1 or newer not found (libavcodec, libavfilter, libavformat, libavutil, libswscale).\n"
		"  Debian/Ubuntu  sudo apt install libavcodec-dev libavfilter-dev libavformat-dev libavutil-dev libswscale-dev\n"
		"  Fedora         sudo dnf install ffmpeg-devel\n"
		"  Arch           sudo pacman -S ffmpeg\n"
		"Or point KLIP_FFMPEG_ROOT at a build produced by scripts/build_ffmpeg.sh.")
endif()

pkg_check_modules(KlipVaapi IMPORTED_TARGET
	libva>=1.14
	libva-drm>=1.14
)

if(NOT KlipVaapi_FOUND)
	message(FATAL_ERROR
		"libva 1.14 or newer not found. Klip encodes on the GPU through VAAPI.\n"
		"  Debian/Ubuntu  sudo apt install libva-dev\n"
		"  Fedora         sudo dnf install libva-devel\n"
		"  Arch           sudo pacman -S libva")
endif()

# A pinned prefix is outside any linker search path. Guarded: a system build must bake in no such path.
if(KLIP_FFMPEG_ROOT)
	list(APPEND CMAKE_BUILD_RPATH "${KLIP_FFMPEG_ROOT}/lib")
	list(APPEND CMAKE_INSTALL_RPATH "${KLIP_FFMPEG_ROOT}/lib")
endif()

message(STATUS "[Klip] libavcodec ${KlipFFmpeg_libavcodec_VERSION}, libva ${KlipVaapi_libva_VERSION}")
