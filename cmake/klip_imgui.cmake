set(KlipImguiRoot ${PROJECT_SOURCE_DIR}/external/imgui)

add_library(imgui STATIC
	${KlipImguiRoot}/imgui.cpp
	${KlipImguiRoot}/imgui_draw.cpp
	${KlipImguiRoot}/imgui_tables.cpp
	${KlipImguiRoot}/imgui_widgets.cpp
	${KlipImguiRoot}/misc/cpp/imgui_stdlib.cpp
	${KlipImguiRoot}/backends/imgui_impl_sdl3.cpp
	${KlipImguiRoot}/backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui SYSTEM PUBLIC ${KlipImguiRoot} ${KlipImguiRoot}/backends ${KlipImguiRoot}/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_FONT IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# Klip's standard library, since imgui_stdlib hands std::string across to Klip's code.
if(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
	target_compile_options(imgui PRIVATE -stdlib=libstdc++)
endif()

KlipSuppressExternalWarnings(imgui)
