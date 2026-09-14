# Read as a property, not linked: a PRIVATE link on a target still lands in INTERFACE_LINK_LIBRARIES as
# $<LINK_ONLY:KlipCompileFlags>, dragging a build-only target into anything that later consumes this one.
function(KlipApplyCompileFlags name)
	target_compile_options(${name} PRIVATE $<TARGET_PROPERTY:KlipCompileFlags,INTERFACE_COMPILE_OPTIONS>)
	target_link_options(${name} PRIVATE $<TARGET_PROPERTY:KlipCompileFlags,INTERFACE_LINK_OPTIONS>)
endfunction()

# Applied after the project flags so -w wins.
function(KlipSuppressExternalWarnings target_name)
	if(NOT TARGET ${target_name})
		message(FATAL_ERROR "KlipSuppressExternalWarnings: '${target_name}' is not a target")
	endif()

	if(MSVC)
		target_compile_options(${target_name} PRIVATE /w)
	else()
		target_compile_options(${target_name} PRIVATE -w)
	endif()
endfunction()
