add_library(KlipEncode STATIC
	public/encode/capabilities.hpp
	public/encode/encoder.hpp
	public/encode/format.hpp
	public/encode/quality.hpp
	public/encode/settings.hpp
	private/log.hpp
	private/log.cpp
	private/capabilities.cpp
	private/encoder.cpp
)

target_include_directories(KlipEncode
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipEncode
	PUBLIC KlipCapturePublic
	PRIVATE TgeLogging Tge::Profiling PkgConfig::KlipFFmpeg PkgConfig::KlipVaapi
)

KlipApplyCompileFlags(KlipEncode)
