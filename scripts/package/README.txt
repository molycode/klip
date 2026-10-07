Klip
====

Records your screen, a single window, or a rectangle of either, to a video file, with the
system's sound and a microphone if you want them. Pick a source, pick where the file goes,
press record.

Requirements
------------
x86-64 (64-bit PC) Linux with glibc 2.35, PipeWire 1.0 and libva 2.20 or newer, such as
Ubuntu 24.04, Debian 13, Fedora 40, Arch Linux, AlmaLinux and RHEL 9, and later releases.
The desktop must run xdg-desktop-portal with a ScreenCast backend, and the graphics card
needs a VAAPI driver that can encode, such as mesa-va-drivers for AMD or intel-media-driver
for Intel. Klip offers only the formats the card accepts, so without such a driver it
offers none.

Install
-------
    ./install.sh

This installs Klip for your user only, under ~/.local, and adds it to the applications
menu. It needs no root. To upgrade, unpack a newer package and run its install.sh.

Uninstall
---------
    ./uninstall.sh

or, once this folder is gone, the copy install.sh keeps:

    sh ~/.local/share/klip/uninstall.sh

Either removes the program, the FFmpeg it carries, its menu entry and its icon. Your
settings and logs stay, and your recordings are never touched:

    Settings  ~/.config/klip/
    Logs      ~/.local/state/klip/logs/

These paths follow $XDG_CONFIG_HOME, $XDG_STATE_HOME and $XDG_DATA_HOME when those are set.

Reporting a problem
-------------------
Klip > About Klip > System has a Copy button for the versions that matter and opens the
logs folder. Send that text and the log of the session that went wrong.

FFmpeg
------
Klip encodes with FFmpeg 9.0.2, which this package carries in lib/ and install.sh puts in
~/.local/lib/klip/. It is used under the GNU Lesser General Public License version 2.1 or
later, unmodified and built without its GPL or non-free parts; ffmpeg-configure.txt holds
the exact configure line. Its source is ffmpeg-9.0.2.tar.xz, published beside this package
on the same release page, and also at https://ffmpeg.org/releases/. Klip loads the
libraries dynamically, so a build of your own of the same FFmpeg version can replace them.

Licence
-------
MIT, see LICENSE. NOTICE lists the code Klip includes or uses under licences of its own,
and the program shows their texts under Klip > About Klip > Licences.
