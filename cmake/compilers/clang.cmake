add_library(KlipCompileFlags INTERFACE)

target_compile_options(KlipCompileFlags INTERFACE
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
	$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libstdc++>
)

message(STATUS "[Klip] Clang ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
