add_library(KlipJson STATIC
	public/json/files.hpp
	public/json/json.hpp
	public/json/syntax_error.hpp
	private/files.cpp
	private/json.cpp
	private/log.hpp
	private/log.cpp
	private/syntax_error.cpp
	private/syntax_error_handler.hpp
	private/syntax_error_handler.cpp
)

target_include_directories(KlipJson
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# SYSTEM, so -Werror never applies to nlohmann's own code.
target_include_directories(KlipJson SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/external/json/single_include)

target_link_libraries(KlipJson PRIVATE TgeBase TgeLogging)

KlipApplyCompileFlags(KlipJson)
