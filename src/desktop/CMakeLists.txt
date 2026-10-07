add_library(KlipDesktop STATIC
	public/desktop/request.hpp
	public/desktop/tray.hpp
	private/log.hpp
	private/log.cpp
	private/menu.hpp
	private/menu.cpp
	private/tray.cpp
)

target_include_directories(KlipDesktop
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipDesktop
	PUBLIC KlipBus TgeBase
	PRIVATE TgeLogging
)

KlipApplyCompileFlags(KlipDesktop)
