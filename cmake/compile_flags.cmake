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
