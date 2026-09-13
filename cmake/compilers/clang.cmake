add_library(KlipCompileFlags INTERFACE)

target_compile_options(KlipCompileFlags INTERFACE
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	# Targets that link Qt re-enable this per target; a later -fexceptions on the command line wins.
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
	$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libstdc++>
)

message(STATUS "[Klip] Clang ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
