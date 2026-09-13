find_package(PkgConfig REQUIRED)

pkg_check_modules(KlipPipeWire IMPORTED_TARGET libpipewire-0.3>=1.0)

if(NOT KlipPipeWire_FOUND)
	message(FATAL_ERROR
		"PipeWire 1.0 or newer not found. Every Klip capture arrives as a PipeWire stream.\n"
		"  Debian/Ubuntu  sudo apt install libpipewire-0.3-dev\n"
		"  Fedora         sudo dnf install pipewire-devel\n"
		"  Arch           sudo pacman -S libpipewire")
endif()

# The public headers alone, so KlipEncode can take SFrame without taking Qt and PipeWire with it.
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
	private/audio_devices.cpp
	private/audio_loop.hpp
	private/audio_loop.cpp
	private/audio_stream.cpp
	private/log.hpp
	private/log.cpp
	private/pipewire.cpp
	private/pipewire_stream.cpp
	private/portal_session.cpp
)

set_target_properties(KlipCapture PROPERTIES AUTOMOC ON)

target_include_directories(KlipCapture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private)

target_link_libraries(KlipCapture
	PUBLIC Qt6::Core KlipCapturePublic
	PRIVATE TgeLogging Tge::Profiling Qt6::DBus PkgConfig::KlipPipeWire
)

KlipApplyCompileFlags(KlipCapture)

# Qt's headers contain try/catch. Applied after the project flags so it lands last and wins.
target_compile_options(KlipCapture PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
