add_library(KlipRecorder STATIC
	public/recorder/audio_choice.hpp
	public/recorder/audio_source.hpp
	public/recorder/decibels.hpp
	public/recorder/format.hpp
	public/recorder/frame_rates.hpp
	public/recorder/labels.hpp
	public/recorder/level_ballistics.hpp
	public/recorder/recorder.hpp
	public/recorder/reveal.hpp
	public/recorder/screen.hpp
	public/recorder/session.hpp
	public/recorder/settings.hpp
	public/recorder/settings_changes.hpp
	public/recorder/source.hpp
	public/recorder/state.hpp
	public/recorder/throughput.hpp
	private/format.cpp
	private/labels.cpp
	private/level_ballistics.cpp
	private/log.hpp
	private/log.cpp
	private/recorder.cpp
	private/session.cpp
)

target_include_directories(KlipRecorder
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipRecorder
	PUBLIC KlipCapture KlipDesktop KlipEncode TgeThreading
	PRIVATE TgeLogging Tge::Profiling
)

KlipApplyCompileFlags(KlipRecorder)
