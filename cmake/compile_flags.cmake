# Selects the compiler flag set for first-party code, creating the KlipCompileFlags INTERFACE library.
# Its own file so the selection stays in one place.

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	include(${CMAKE_CURRENT_LIST_DIR}/compilers/msvc.cmake)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
	include(${CMAKE_CURRENT_LIST_DIR}/compilers/clang.cmake)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	include(${CMAKE_CURRENT_LIST_DIR}/compilers/gcc.cmake)
else()
	message(FATAL_ERROR "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}'. Supported: GNU, Clang, MSVC.")
endif()

# One option rather than raw -fsanitize flags in a preset, so "which sanitizer" is stated once and
# the build system derives the rest - including switching rpmalloc off, which a hand-written preset
# has already been seen to forget.
set(KLIP_SANITIZER "none" CACHE STRING "Sanitizer to build with: none, address, undefined or thread")
set_property(CACHE KLIP_SANITIZER PROPERTY STRINGS none address undefined thread)

if(NOT KLIP_SANITIZER STREQUAL "none")
	if(NOT KLIP_SANITIZER MATCHES "^(address|undefined|thread)$")
		message(FATAL_ERROR "KLIP_SANITIZER is '${KLIP_SANITIZER}'; expected none, address, undefined or thread")
	endif()

	if(MSVC)
		message(FATAL_ERROR "KLIP_SANITIZER has never been exercised with MSVC; wire it before using it")
	endif()

	target_compile_options(KlipCompileFlags INTERFACE -fsanitize=${KLIP_SANITIZER} -fno-omit-frame-pointer)
	target_link_options(KlipCompileFlags INTERFACE -fsanitize=${KLIP_SANITIZER})

	# rpmalloc serves new and delete from pages it takes straight from mmap, so ASan never sees the
	# allocation and TSan never sees the synchronisation. Forced rather than defaulted because leaving
	# it on does not fail the build, it just makes the run quietly useless.
	if(KLIP_SANITIZER MATCHES "^(address|thread)$")
		set(TGE_ENABLE_MEMORY_TRACKING OFF CACHE BOOL "Enable memory tracking system" FORCE)
		message(STATUS "[Klip] Memory tracking forced off for the ${KLIP_SANITIZER} sanitizer")
	endif()

	message(STATUS "[Klip] Sanitizer enabled: ${KLIP_SANITIZER}")
endif()
