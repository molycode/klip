add_library(KlipConfig STATIC
	public/config/settings_store.hpp
	public/config/xdg_paths.hpp
	private/klip_conf.hpp
	private/klip_conf.cpp
	private/log.hpp
	private/log.cpp
	private/settings_document.hpp
	private/settings_json.hpp
	private/settings_json.cpp
	private/settings_json_error.hpp
	private/settings_store.cpp
	private/xdg_paths.cpp
)

target_include_directories(KlipConfig
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipConfig
	PUBLIC KlipRecorder TgeBase
	PRIVATE KlipJson TgeLogging
)

KlipApplyCompileFlags(KlipConfig)
