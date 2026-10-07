add_library(KlipCompileFlags INTERFACE)

target_compile_options(KlipCompileFlags INTERFACE
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

message(STATUS "[Klip] GCC ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
