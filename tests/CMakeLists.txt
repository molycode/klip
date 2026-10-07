find_program(KLIP_DBUS_RUN_SESSION dbus-run-session)

if(NOT KLIP_DBUS_RUN_SESSION)
	message(FATAL_ERROR
		"dbus-run-session not found. Every test run gets a private session bus from it.\n"
		"  Debian/Ubuntu  sudo apt install dbus-daemon\n"
		"  Fedora         sudo dnf install dbus-daemon\n"
		"  Arch           sudo pacman -S dbus")
endif()

add_executable(KlipTests
	main.cpp
	tge_environment.hpp
	tge_environment.cpp
	bus/test_connection.cpp
	capture/fake_portal.hpp
	capture/fake_portal.cpp
	capture/test_portal_session.cpp
	desktop/fake_panel.hpp
	desktop/fake_panel.cpp
	desktop/test_tray.cpp
)

target_include_directories(KlipTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(KlipTests PRIVATE
	KlipBus
	KlipCapture
	KlipDesktop
	TgeInit
	TgeLogging
	Tge::Testing
	GTest::gtest
)

KlipApplyCompileFlags(KlipTests)

add_test(NAME KlipTests
	COMMAND ${KLIP_DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-session.conf --
	        $<TARGET_FILE:KlipTests>)

# A finding fails the suite, rather than scrolling past in its output.
set_tests_properties(KlipTests PROPERTIES ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
