cmake_minimum_required(VERSION 3.28.0)

project(Klip VERSION 0.1.0 LANGUAGES C CXX)

# C is here for tge-core's rpmalloc, not for Klip's own code.
if(PROJECT_IS_TOP_LEVEL)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	set(CMAKE_CONFIGURATION_TYPES Debug Release)
	# No target here declares a module, and leaving this on makes every Clang build need clang-scan-deps,
	# which most distributions ship in a separate package.
	set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

# Named and checked, or a too-old toolchain reports the floor as a template backtrace out of <print>.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
	message(FATAL_ERROR
		"Klip needs GCC 13 or newer for C++23; found ${CMAKE_CXX_COMPILER_VERSION}. "
		"Install a newer g++ and configure with -DCMAKE_CXX_COMPILER=g++-<version>.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
	message(FATAL_ERROR
		"Klip needs Clang 18 or newer for C++23; found ${CMAKE_CXX_COMPILER_VERSION}.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform.cmake)

# Both guarded so the tree can be vendored under a parent that already provided them.
if(NOT COMMAND KlipApplyCompileFlags)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_targets.cmake)
endif()

if(NOT TARGET KlipCompileFlags)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_flags.cmake)
endif()

# Generator expressions rather than a CMAKE_BUILD_TYPE test, so these survive a multi-config generator.
add_compile_definitions($<$<CONFIG:Debug>:KLIP_DEBUG>)
add_compile_definitions($<$<CONFIG:Debug>:TGE_DEBUG_ENABLED>)
# tge-core wraps every log call in this, std::format included, so a Release build pays nothing for the
# ones it does not make. Decided at configure time because tge-core reads it as an option, which is why
# this one is not a generator expression like its neighbours.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(TGE_ENABLE_LOGGING OFF)
else()
	add_compile_definitions(TGE_LOGGING_ENABLED)
endif()

# 6.4 is Ubuntu 24.04's; Klip uses only long-stable API.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Gui Network Widgets)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_ffmpeg.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_tracy.cmake)

# After Tracy: tge-core picks its profiling backend from whether Tracy::TracyClient already exists.
add_subdirectory(external/tge-core EXCLUDE_FROM_ALL)

add_subdirectory(src/capture)
add_subdirectory(src/encode)
add_subdirectory(src/app)

# Guarded like the install rules it undoes: a parent vendoring Klip uninstalls its own application, not
# this one. Top level rather than beside those rules in src/app, because the manifest it reads records
# what every subdirectory installed.
if(PROJECT_IS_TOP_LEVEL)
	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND}
			-DKLIP_INSTALL_MANIFEST=${CMAKE_BINARY_DIR}/install_manifest.txt
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake
		COMMENT "Removing the files that install recorded"
		VERBATIM)
endif()

message(STATUS "[Klip] CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
message(STATUS "[Klip] CMAKE_GENERATOR = ${CMAKE_GENERATOR}")
message(STATUS "[Klip] CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message(STATUS "[Klip] CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "[Klip] CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
