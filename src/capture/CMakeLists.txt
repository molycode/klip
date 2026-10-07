find_package(PkgConfig REQUIRED)

pkg_check_modules(KlipPipeWire IMPORTED_TARGET libpipewire-0.3>=1.0)

if(NOT KlipPipeWire_FOUND)
	message(FATAL_ERROR
		"PipeWire 1.0 or newer not found. Every Klip capture arrives as a PipeWire stream.\n"
		"  Debian/Ubuntu  sudo apt install libpipewire-0.3-dev\n"
		"  Fedora         sudo dnf install pipewire-devel\n"
		"  Arch           sudo pacman -S libpipewire")
endif()

# The public headers alone, so KlipEncode can take SFrame without taking PipeWire and sd-bus with it.
add_library(KlipCapturePublic INTERFACE)

target_include_directories(KlipCapturePublic INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/public)
target_link_libraries(KlipCapturePublic INTERFACE TgeBase)

add_library(KlipCapture STATIC
	public/capture/audio_buffer.hpp
	public/capture/audio_device.hpp
	public/capture/audio_devices.hpp
	public/capture/audio_stream.hpp
	public/capture/frame.hpp
	public/capture/pipewire.hpp
	public/capture/pipewire_stream.hpp
	public/capture/portal_session.hpp
	public/capture/screenshot.hpp
	private/audio_devices.cpp
	private/audio_loop.hpp
	private/audio_loop.cpp
	private/audio_stream.cpp
	private/log.hpp
	private/log.cpp
	private/pipewire.cpp
	private/pipewire_stream.cpp
	private/portal_session.cpp
	private/screenshot.cpp
)

target_include_directories(KlipCapture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private)

target_link_libraries(KlipCapture
	PUBLIC KlipCapturePublic
	PRIVATE KlipBus TgeLogging Tge::Profiling PkgConfig::KlipPipeWire
)

KlipApplyCompileFlags(KlipCapture)
