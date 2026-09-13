# Off unless asked for. Klip's own code only ever uses tge-core's TGE_PROFILE_* markers, which compile to
# nothing when this leaves Tracy::TracyClient undefined -- the backend is chosen here and nowhere else.
# KLIP_TRACY_ROOT names a Tracy source checkout, the same shape as KLIP_FFMPEG_ROOT, because Tracy is a
# developer tool rather than something a user compiling Klip should have to supply.

option(KLIP_ENABLE_TRACY "Build the Tracy profiler client into Klip (needs KLIP_TRACY_ROOT)" OFF)

if(KLIP_ENABLE_TRACY)
	if(NOT KLIP_TRACY_ROOT AND DEFINED ENV{KLIP_TRACY_ROOT})
		set(KLIP_TRACY_ROOT "$ENV{KLIP_TRACY_ROOT}" CACHE PATH "Tracy source checkout")
	endif()

	if(NOT EXISTS "${KLIP_TRACY_ROOT}/public/tracy/Tracy.hpp")
		message(FATAL_ERROR
			"KLIP_ENABLE_TRACY is ON but KLIP_TRACY_ROOT does not name a Tracy checkout.\n"
			"  git clone https://github.com/wolfpld/tracy\n"
			"then configure with -DKLIP_TRACY_ROOT=<that path>. The capture tools must be built from the "
			"same checkout: a client and a server of different versions refuse to handshake.")
	endif()

	file(STRINGS "${KLIP_TRACY_ROOT}/public/common/TracyVersion.hpp" KlipTracyVersionLines
		REGEX "constexpr int (Major|Minor|Patch)")
	string(REGEX MATCHALL "[0-9]+" KlipTracyVersion "${KlipTracyVersionLines}")
	list(JOIN KlipTracyVersion "." KlipTracyVersion)

	# Upstream defaults this OFF and it is what carries TRACY_ENABLE to consumers, so leaving it unset
	# compiles tge-core's Tracy branch against the no-op headers, where TracyConcat does not exist.
	set(TRACY_ENABLE ON CACHE BOOL "" FORCE)

	# On-demand collects only while a server is attached, which discards everything before it connects --
	# and startup is part of what this exists to measure.
	set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)

	add_subdirectory(${KLIP_TRACY_ROOT} tracy EXCLUDE_FROM_ALL)

	KlipSuppressExternalWarnings(TracyClient)

	message(STATUS "[Klip] Tracy ${KlipTracyVersion} client: profiling markers are live")
endif()
