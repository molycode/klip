add_library(KlipRecorder STATIC
	public/recorder/session.hpp
	private/log.hpp
	private/log.cpp
	private/session.cpp
)

target_include_directories(KlipRecorder
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipRecorder
	PUBLIC KlipCapture KlipEncode TgeThreading
	PRIVATE TgeLogging Tge::Profiling
)

KlipApplyCompileFlags(KlipRecorder)
