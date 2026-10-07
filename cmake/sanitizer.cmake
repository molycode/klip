# One option rather than raw -fsanitize flags in a preset, so "which sanitizer" is stated once and
# the build system derives the rest - including switching rpmalloc off, which a hand-written preset
# has already been seen to forget.
set(KLIP_SANITIZER "none" CACHE STRING
	"Sanitizer to build with: none, address, undefined, address,undefined or thread")
set_property(CACHE KLIP_SANITIZER PROPERTY STRINGS none address undefined address,undefined thread)

if(NOT KLIP_SANITIZER STREQUAL "none")
	if(NOT KLIP_SANITIZER MATCHES "^(address|undefined|address,undefined|thread)$")
		message(FATAL_ERROR
			"KLIP_SANITIZER is '${KLIP_SANITIZER}'; expected none, address, undefined, address,undefined or thread")
	endif()

	if(MSVC)
		message(FATAL_ERROR "KLIP_SANITIZER has never been exercised with MSVC; wire it before using it")
	endif()

	# Directory-wide, so tge-core and anything else compiled from source below here is instrumented too.
	# TSan in particular needs both sides of a handover instrumented to see the edge between them.
	add_compile_options(-fsanitize=${KLIP_SANITIZER} -fno-omit-frame-pointer)
	add_link_options(-fsanitize=${KLIP_SANITIZER})

	# rpmalloc serves new and delete from pages it takes straight from mmap, so ASan never sees the
	# allocation and TSan never sees the synchronisation. Forced rather than defaulted because leaving
	# it on does not fail the build, it just makes the run quietly useless.
	if(KLIP_SANITIZER MATCHES "address|thread")
		set(TGE_ENABLE_GLOBAL_ALLOCATOR OFF CACHE BOOL "Route global new and delete through rpmalloc" FORCE)
		message(STATUS "[Klip] Global allocator forced off for the ${KLIP_SANITIZER} sanitizer")
	endif()

	message(STATUS "[Klip] Sanitizer enabled: ${KLIP_SANITIZER}")
endif()
