cmake_minimum_required(VERSION 3.30.0)

project(Klip VERSION 1.0.0 LANGUAGES C CXX)

# C is here for tge-core's rpmalloc, not for Klip's own code.
if(PROJECT_IS_TOP_LEVEL)
	set(CMAKE_CXX_STANDARD 26)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	set(CMAKE_CONFIGURATION_TYPES Debug RelWithDebInfo Release)
	# No target here declares a module, and leaving this on makes every Clang build need clang-scan-deps,
	# which most distributions ship in a separate package.
	set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

# Named and checked, or a too-old toolchain reports the floor as a template backtrace out of <print>.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
	message(FATAL_ERROR
		"Klip needs GCC 14 or newer for C++26; found ${CMAKE_CXX_COMPILER_VERSION}. "
		"Install a newer g++ and configure with -DCMAKE_CXX_COMPILER=g++-<version>.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19)
	# libstdc++ gates std::expected on a __cpp_concepts value that Clang 18 does not report.
	message(FATAL_ERROR
		"Klip needs Clang 19 or newer for C++26; found ${CMAKE_CXX_COMPILER_VERSION}. "
		"Install a newer clang++ and configure with -DCMAKE_CXX_COMPILER=clang++-<version>.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform.cmake)

# Both guarded so the tree can be vendored under a parent that already provided them.
if(NOT COMMAND KlipApplyCompileFlags)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_targets.cmake)
endif()

if(NOT TARGET KlipCompileFlags)
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_flags.cmake)
endif()

# Outside that guard, so a parent that already made KlipCompileFlags still builds Klip instrumented; and
# before Tracy and tge-core, so they are.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizer.cmake)

# Generator expressions rather than a CMAKE_BUILD_TYPE test, so these survive a multi-config generator.
add_compile_definitions($<$<CONFIG:Debug>:KLIP_DEBUG>)
add_compile_definitions($<$<CONFIG:Debug>:TGE_DEBUG_ENABLED>)
add_compile_definitions(TGE_LOGGING_ENABLED)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_ffmpeg.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_tracy.cmake)

option(KLIP_BUILD_TESTS "Build Klip's test suite, from the googletest submodule" OFF)

# A clone without --recurse-submodules has the mount points and nothing in them, which otherwise fails far from
# here, as a missing header.
set(KlipSubmoduleFiles
	external/tge-core/CMakeLists.txt
	external/json/single_include/nlohmann/json.hpp
	external/sdl/CMakeLists.txt
	external/imgui/imgui.h
)

if(KLIP_BUILD_TESTS)
	list(APPEND KlipSubmoduleFiles external/googletest/CMakeLists.txt)
endif()

foreach(KlipSubmoduleFile IN LISTS KlipSubmoduleFiles)
	if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${KlipSubmoduleFile})
		message(FATAL_ERROR
			"${KlipSubmoduleFile} is missing: the submodules were not checked out. From the source directory:\n"
			"  git submodule update --init --recursive")
	endif()
endforeach()

if(KLIP_BUILD_TESTS)
	# Normal variables, which googletest's options honour; neither its installation nor gmock is wanted.
	set(INSTALL_GTEST OFF)
	set(BUILD_GMOCK OFF)

	# Before tge-core, whose Testing component takes googletest from whoever added it first.
	add_subdirectory(external/googletest ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL SYSTEM)
	KlipSuppressExternalWarnings(gtest)

	# A normal variable, because a cache entry would lose to the OFF an earlier configure already cached.
	set(TGE_CORE_TESTING ON)

	enable_testing()
endif()

# After Tracy: tge-core picks its profiling backend from whether Tracy::TracyClient already exists.
add_subdirectory(external/tge-core EXCLUDE_FROM_ALL)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_sdl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_imgui.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/klip_embed.cmake)

add_subdirectory(src/bus)
add_subdirectory(src/capture)
add_subdirectory(src/encode)
add_subdirectory(src/desktop)
add_subdirectory(src/recorder)
add_subdirectory(src/json)
add_subdirectory(src/config)
add_subdirectory(src/app)

if(KLIP_BUILD_TESTS)
	add_subdirectory(tests)
endif()

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
