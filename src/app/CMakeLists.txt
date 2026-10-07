add_executable(Klip
	private/activation.hpp
	private/activation.cpp
	private/application.hpp
	private/application.cpp
	private/embedded_fonts.hpp
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
