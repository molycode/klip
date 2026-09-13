# Here rather than in the toolchain files: a user building the source package configures without one.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_compile_definitions(KLIP_PLATFORM_LINUX)
	message(STATUS "[Klip] Platform: Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	add_compile_definitions(KLIP_PLATFORM_WINDOWS)
	add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
	message(STATUS "[Klip] Platform: Windows")
else()
	message(FATAL_ERROR
		"Klip has no platform settings for '${CMAKE_SYSTEM_NAME}'. Add a block to cmake/platform.cmake.")
endif()
