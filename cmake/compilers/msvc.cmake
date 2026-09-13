add_library(KlipCompileFlags INTERFACE)

target_compile_options(KlipCompileFlags INTERFACE
	/W4
	/WX
	/wd4100
	/permissive-
)

message(STATUS "[Klip] MSVC ${CMAKE_CXX_COMPILER_VERSION} compiler flags configured")
