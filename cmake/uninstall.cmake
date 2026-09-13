# Run by the `uninstall` target, never included: CMake writes an install manifest but offers nothing that
# reads it back. The paths in it are absolute, so whichever prefix that install ran with is already baked
# into them and does not have to be named a second time.

if(NOT EXISTS "${KLIP_INSTALL_MANIFEST}")
	message(FATAL_ERROR
		"${KLIP_INSTALL_MANIFEST} is not there: nothing has been installed from this build directory.")
endif()

file(STRINGS "${KLIP_INSTALL_MANIFEST}" KlipInstalledFiles)
list(LENGTH KlipInstalledFiles KlipNumListed)

set(KlipNumRemoved 0)
set(KlipRefused "")

# Files only. The directories they sit in -- bin, share/applications, the icon theme -- are the system's,
# and pruning one because Klip happened to empty it would take somebody else's install down with it.
foreach(KlipInstalledFile IN LISTS KlipInstalledFiles)
	if(EXISTS "${KlipInstalledFile}" OR IS_SYMLINK "${KlipInstalledFile}")
		file(REMOVE "${KlipInstalledFile}")

		# file(REMOVE) reports nothing at all, so without this a prefix that refused every unlink would
		# still finish clean and the files would still be there.
		if(EXISTS "${KlipInstalledFile}" OR IS_SYMLINK "${KlipInstalledFile}")
			list(APPEND KlipRefused "${KlipInstalledFile}")
		else()
			message(STATUS "Uninstalling: ${KlipInstalledFile}")
			math(EXPR KlipNumRemoved "${KlipNumRemoved} + 1")
		endif()
	endif()
endforeach()

if(KlipRefused)
	list(JOIN KlipRefused "\n  " KlipRefusedList)
	message(FATAL_ERROR
		"Could not remove:\n  ${KlipRefusedList}\n"
		"Installed with sudo? Uninstall with it too. The manifest is left in place either way.")
endif()

file(REMOVE "${KLIP_INSTALL_MANIFEST}")

message(STATUS "[Klip] Removed ${KlipNumRemoved} of the ${KlipNumListed} files the manifest listed")
