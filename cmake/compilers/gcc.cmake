add_library(KlipCompileFlags INTERFACE)

target_compile_options(KlipCompileFlags INTERFACE
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	# Targets that link Qt re-enable this per target; a later -fexceptions on the command line wins.
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

message(STATUS "[Klip] GCC ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
