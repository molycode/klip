add_executable(Klip
	private/level_meter.hpp
	private/level_meter.cpp
	private/log.hpp
	private/log.cpp
	private/main.cpp
	private/main_window.hpp
	private/main_window.cpp
	private/region_selector.hpp
	private/region_selector.cpp
	private/screenshot.hpp
	private/screenshot.cpp
	private/single_instance.hpp
	private/single_instance.cpp
	private/session.hpp
	private/session.cpp
	private/tray_adaptor.hpp
	private/tray_adaptor.cpp
	private/tray_icon.hpp
	private/tray_icon.cpp
	private/tray_menu.hpp
	private/tray_menu.cpp
	private/tray_types.hpp
	private/tray_types.cpp
)

set_target_properties(Klip PROPERTIES AUTOMOC ON)

set_target_properties(Klip PROPERTIES OUTPUT_NAME "klip")

target_include_directories(Klip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private)

target_compile_definitions(Klip PRIVATE KLIP_VERSION="${PROJECT_VERSION}")

target_link_libraries(Klip PRIVATE
	KlipCapture
	KlipEncode
	TgeInit
	Tge::Profiling
	TgeLogging
	TgeThreading
	Qt6::Widgets
	Qt6::Network
	Qt6::DBus
)

KlipApplyCompileFlags(Klip)

# Linking Qt means compiling its headers; see src/capture/CMakeLists.txt.
target_compile_options(Klip PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)

# Guarded: a parent vendoring Klip is installing its own application, not this one.
if(PROJECT_IS_TOP_LEVEL)
	include(GNUInstallDirs)

	install(TARGETS Klip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	# Named klip.desktop because main.cpp calls setDesktopFileName("klip"); a Wayland compositor matches
	# the window to its launcher by that name, and an entry filed under any other loses the icon.
	install(FILES ${PROJECT_SOURCE_DIR}/data/klip.desktop
	        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

	install(FILES ${PROJECT_SOURCE_DIR}/data/klip.svg
	        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
endif()
