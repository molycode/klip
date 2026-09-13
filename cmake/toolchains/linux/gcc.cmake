set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cache variable first, environment as a fallback: a preset's `environment` block never reaches an IDE's
# configure step, so an env-only setting is silently lost there.
if(NOT KLIP_GCC_PATH AND DEFINED ENV{KLIP_GCC_PATH})
	set(KLIP_GCC_PATH "$ENV{KLIP_GCC_PATH}" CACHE PATH "GCC installation root")
endif()

# try_compile re-runs this file in a scratch project that inherits the environment but NOT the cache, so the
# compiler-ABI probe would otherwise fall back to the system GCC and measure the wrong toolchain.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES KLIP_GCC_PATH)

if(NOT DEFINED CMAKE_C_COMPILER)
	if(KLIP_GCC_PATH)
		set(CMAKE_C_COMPILER "${KLIP_GCC_PATH}/bin/gcc")
	else()
		set(CMAKE_C_COMPILER "gcc")
	endif()
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER)
	if(KLIP_GCC_PATH)
		set(CMAKE_CXX_COMPILER "${KLIP_GCC_PATH}/bin/g++")
	else()
		set(CMAKE_CXX_COMPILER "g++")
	endif()
endif()

# Only when a root was given: a package a user builds must not bake in a path that exists on one machine.
if(KLIP_GCC_PATH)
	set(CMAKE_BUILD_RPATH "${KLIP_GCC_PATH}/lib64")
	set(CMAKE_INSTALL_RPATH "${KLIP_GCC_PATH}/lib64")
	message(STATUS "[Klip] Using GCC from KLIP_GCC_PATH: ${KLIP_GCC_PATH}")
else()
	message(STATUS "[Klip] Using system GCC (set KLIP_GCC_PATH to use a pinned GCC)")
endif()
