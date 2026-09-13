set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cache variable first, environment as a fallback: a preset's `environment` block never reaches an IDE's
# configure step, so an env-only setting is silently lost there.
if(NOT KLIP_CLANG_PATH AND DEFINED ENV{KLIP_CLANG_PATH})
	set(KLIP_CLANG_PATH "$ENV{KLIP_CLANG_PATH}" CACHE PATH "Clang installation root")
endif()

if(NOT KLIP_GCC_PATH AND DEFINED ENV{KLIP_GCC_PATH})
	set(KLIP_GCC_PATH "$ENV{KLIP_GCC_PATH}" CACHE PATH "GCC installation supplying libstdc++ to Clang builds")
endif()

# try_compile re-runs this file in a scratch project that inherits the environment but NOT the cache, so
# without this the compiler-ABI probe would measure a different libstdc++ than the build then uses.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES KLIP_CLANG_PATH KLIP_GCC_PATH)

if(KLIP_CLANG_PATH)
	set(CMAKE_C_COMPILER "${KLIP_CLANG_PATH}/bin/clang" CACHE FILEPATH "" FORCE)
	set(CMAKE_CXX_COMPILER "${KLIP_CLANG_PATH}/bin/clang++" CACHE FILEPATH "" FORCE)
elseif(NOT DEFINED CMAKE_C_COMPILER)
	set(CMAKE_C_COMPILER "clang")
	set(CMAKE_CXX_COMPILER "clang++")
endif()

# Clang has no standard library of its own here, so point it at the intended GCC's libstdc++ and bake the
# RPATH so the binary finds that same one at runtime.
if(KLIP_GCC_PATH)
	file(GLOB _gcc_ver_dirs LIST_DIRECTORIES true "${KLIP_GCC_PATH}/lib/gcc/x86_64-pc-linux-gnu/*")
	list(SORT _gcc_ver_dirs COMPARE NATURAL ORDER DESCENDING)
	list(GET _gcc_ver_dirs 0 _gcc_install_dir)
	add_compile_options(--gcc-install-dir=${_gcc_install_dir})
	add_link_options(--gcc-install-dir=${_gcc_install_dir})
	set(CMAKE_BUILD_RPATH "${KLIP_GCC_PATH}/lib64")
	set(CMAKE_INSTALL_RPATH "${KLIP_GCC_PATH}/lib64")
	message(STATUS "[Klip] Using GCC libstdc++ from KLIP_GCC_PATH: ${KLIP_GCC_PATH}")
else()
	message(STATUS "[Klip] Using system Clang and whichever libstdc++ it selects")
endif()

if(KLIP_CLANG_PATH)
	message(STATUS "[Klip] Using Clang from KLIP_CLANG_PATH: ${KLIP_CLANG_PATH}")
else()
	message(STATUS "[Klip] Using system Clang (set KLIP_CLANG_PATH to use a pinned Clang)")
endif()
