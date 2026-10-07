# Static, with every platform library loaded by soname at run time, so one binary serves X11 and Wayland anywhere.
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

foreach(KlipUnusedSubsystem AUDIO CAMERA GPU JOYSTICK HAPTIC HIDAPI POWER SENSOR TRAY)
	set(SDL_${KlipUnusedSubsystem} OFF CACHE BOOL "" FORCE)
endforeach()

foreach(KlipUnusedFeature X11_XSCRNSAVER X11_XTEST FRIBIDI LIBTHAI IBUS LIBUDEV KMSDRM LIBURING OFFSCREEN)
	set(SDL_${KlipUnusedFeature} OFF CACHE BOOL "" FORCE)
endforeach()

add_subdirectory(${PROJECT_SOURCE_DIR}/external/sdl ${CMAKE_BINARY_DIR}/sdl EXCLUDE_FROM_ALL SYSTEM)
KlipSuppressExternalWarningsInDirectory(${PROJECT_SOURCE_DIR}/external/sdl)

# SDL leaves out a backend whose development files are missing and says nothing, so the build would succeed and
# the window would lack a title bar on GNOME (libdecor), fall back to XWayland, or never open a file dialog (D-Bus).
set(KlipSdlMissing "")

foreach(KlipSdlBackend WAYLAND WAYLAND_LIBDECOR X11 DBUS OPENGL)
	get_directory_property(KlipSdlHas DIRECTORY ${PROJECT_SOURCE_DIR}/external/sdl DEFINITION HAVE_${KlipSdlBackend})

	if(NOT KlipSdlHas)
		list(APPEND KlipSdlMissing ${KlipSdlBackend})
	endif()
endforeach()

if(KlipSdlMissing)
	list(JOIN KlipSdlMissing ", " KlipSdlMissingText)
	message(FATAL_ERROR
		"SDL found no development files for: ${KlipSdlMissingText}. Klip's window needs all of them.\n"
		"  Debian/Ubuntu  sudo apt install libwayland-dev libxkbcommon-dev libegl-dev libgl-dev libdecor-0-dev "
		"libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libdbus-1-dev\n"
		"  Fedora         sudo dnf install wayland-devel libxkbcommon-devel mesa-libEGL-devel mesa-libGL-devel "
		"libdecor-devel libX11-devel libXext-devel libXcursor-devel libXi-devel libXfixes-devel libXrandr-devel "
		"dbus-devel\n"
		"  Arch           sudo pacman -S wayland libxkbcommon libglvnd libdecor libx11 libxext libxcursor libxi "
		"libxfixes libxrandr dbus")
endif()
