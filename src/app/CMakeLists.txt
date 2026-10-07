add_executable(Klip
	private/about_dialog.hpp
	private/about_dialog.cpp
	private/activation.hpp
	private/activation.cpp
	private/application.hpp
	private/application.cpp
	private/embedded_fonts.hpp
	private/embedded_licences.hpp
	private/folder_dialog.hpp
	private/folder_dialog.cpp
	private/folder_dialog_mailbox.hpp
	private/folder_dialog_result.hpp
	private/folder_dialog_state.hpp
	private/fonts.hpp
	private/fonts.cpp
	private/format_to.hpp
	private/icons.hpp
	private/level_meter.hpp
	private/level_meter.cpp
	private/licensed_component.hpp
	private/licensed_components.hpp
	private/licensed_components.cpp
	private/log.hpp
	private/log.cpp
	private/main.cpp
	private/main_view.hpp
	private/main_view.cpp
	private/primary_display.hpp
	private/primary_display.cpp
	private/region_selector.hpp
	private/region_selector.cpp
	private/selector_answer.hpp
	private/start_flow.hpp
	private/start_flow.cpp
	private/start_step.hpp
	private/theme.hpp
	private/theme.cpp
	private/ui_scale.hpp
	private/ui_scale.cpp
	private/view_intents.hpp
)

set_target_properties(Klip PROPERTIES OUTPUT_NAME "klip")

target_include_directories(Klip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private)

target_compile_definitions(Klip PRIVATE KLIP_VERSION="${PROJECT_VERSION}")

target_link_libraries(Klip PRIVATE
	KlipBus
	KlipCapture
	KlipConfig
	KlipDesktop
	KlipEncode
	KlipRecorder
	TgeInit
	Tge::Profiling
	TgeLogging
	TgeThreading
	imgui
	SDL3::SDL3-static
)

KlipApplyCompileFlags(Klip)

KlipEmbedFile(Klip RobotoMedium ${PROJECT_SOURCE_DIR}/external/imgui/misc/fonts/Roboto-Medium.ttf)
KlipEmbedFile(Klip FontAwesomeSolid ${PROJECT_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf)
KlipEmbedFile(Klip KlipLicence ${PROJECT_SOURCE_DIR}/LICENSE)
KlipEmbedFile(Klip ImGuiLicence ${PROJECT_SOURCE_DIR}/external/imgui/LICENSE.txt)
KlipEmbedFile(Klip SdlLicence ${PROJECT_SOURCE_DIR}/external/sdl/LICENSE.txt)
KlipEmbedFile(Klip SdlBundledLicences ${PROJECT_SOURCE_DIR}/assets/licences/sdl-bundled.txt)
KlipEmbedFile(Klip SdlWaylandLicences ${PROJECT_SOURCE_DIR}/assets/licences/sdl-wayland-protocols.txt)
KlipEmbedFile(Klip JsonLicence ${PROJECT_SOURCE_DIR}/external/json/LICENSE.MIT)
KlipEmbedFile(Klip JsonBundledLicences ${PROJECT_SOURCE_DIR}/assets/licences/json-bundled.txt)
KlipEmbedFile(Klip FfmpegLicence ${PROJECT_SOURCE_DIR}/assets/licences/ffmpeg.txt)
KlipEmbedFile(Klip TgeCoreLicence ${PROJECT_SOURCE_DIR}/external/tge-core/LICENSE)
KlipEmbedFile(Klip RpmallocLicence ${PROJECT_SOURCE_DIR}/external/tge-core/external/rpmalloc/LICENSE)
KlipEmbedFile(Klip RobotoLicence ${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE-Roboto.txt)
KlipEmbedFile(Klip FontAwesomeLicence ${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE-FontAwesome.txt)

# Guarded: a parent vendoring Klip is installing its own application, not this one.
if(PROJECT_IS_TOP_LEVEL)
	include(GNUInstallDirs)

	install(TARGETS Klip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	# Named klip.desktop because the window's app_id is "klip"; a Wayland compositor matches the window to its
	# launcher by that name, and an entry filed under any other loses the icon.
	install(FILES ${PROJECT_SOURCE_DIR}/data/klip.desktop
	        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

	install(FILES ${PROJECT_SOURCE_DIR}/data/klip.svg
	        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
endif()
