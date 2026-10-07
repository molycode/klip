find_package(PkgConfig REQUIRED)

# 246 is where sd_bus_get_unique_name, the newest call Klip makes, arrived.
pkg_check_modules(KlipSystemd IMPORTED_TARGET libsystemd>=246)

if(NOT KlipSystemd_FOUND)
	message(FATAL_ERROR
		"libsystemd 246 or newer not found. Klip reaches the desktop portal over D-Bus through its sd-bus.\n"
		"  Debian/Ubuntu  sudo apt install libsystemd-dev\n"
		"  Fedora         sudo dnf install systemd-devel\n"
		"  Arch           sudo pacman -S systemd-libs")
endif()

add_library(KlipBus STATIC
	public/bus/connection.hpp
	public/bus/portal.hpp
	private/connection.cpp
	private/log.hpp
	private/log.cpp
	private/portal.cpp
)

target_include_directories(KlipBus
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

target_link_libraries(KlipBus
	PUBLIC TgeBase TgeThreading PkgConfig::KlipSystemd
	PRIVATE TgeLogging
)

KlipApplyCompileFlags(KlipBus)
