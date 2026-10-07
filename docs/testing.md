# Checking Klip

Klip's bugs are runtime ones. A compiler cannot see a frame arriving on the wrong thread, a buffer handed
back while the GPU still reads it, or a file that is a valid MP4 containing nothing. So the checks here
run the application and then ask whether the file holds a recording -- and each of them ends in a verdict
rather than a log.

The runtime ones need a screen cast grant, so they belong to whoever is at the machine and none can
gate CI. The test suite and the last three need only a compiler, and the floor Docker besides.

## The test suite

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug
ctest --test-dir build/gcc-Debug --output-on-failure
```

`KLIP_BUILD_TESTS` builds `KlipTests` from the googletest submodule; the Debug and sanitizer presets turn
it on, and a user's build never does. ctest runs it under `dbus-run-session` with `tests/dbus-session.conf`,
a session bus of its own with no service directories, so a test that asks for the desktop portal gets the
suite's fake or nothing -- never the real one started on demand. It also points `PIPEWIRE_REMOTE` at a
socket that does not exist, so the audio device lists are empty on every machine instead of whatever this
one has plugged in; the recorder's tests are written against that. Run it under the `asanubsan` and `tsan`
presets as well as Debug: UBSan is set to halt there, so a finding fails the suite rather than scrolling
past.

It reaches what a recording on a working desktop never does: a fake ScreenCast portal, on a connection of
its own, answers every way the real one can -- a dismissed picker, refused sources, a stream with no size,
several streams, no portal at all, one too old to persist a grant, and a cast the compositor withdraws --
and TSan watches the bus thread hand each answer over.

## The smoke test

```bash
python3 scripts/smoke_test.py --seconds 15
```

Launches Klip, starts and stops it through its tray item on the session bus, and gates the file with
`ffprobe`: codec, dimensions against what Klip's own log claims, duration, packet count, the audio track,
and a decode on the card. Point it at binaries with `--ffprobe` / `--ffmpeg`, or `$KLIP_FFPROBE` /
`$KLIP_FFMPEG`, since a distribution FFmpeg may not decode what Klip writes.

The content gate holds the picture itself to a portal screenshot taken halfway through: both shrunk to
160x90 grey, and the best correlation among the frames around that moment must reach 0.8. Measured: a
correct recording scores 1.00 on a still desktop and 0.98 with a 60 fps test pattern moving on it, while
a DMA-BUF imported with the wrong modifier -- tiles read as linear -- scores -0.02, having passed every
other gate including the decode. It compares luma only, so a red and blue swap still passes, and it is
skipped for a window source and for a screenshot spanning more than one monitor.

`--quit` ends through the tray menu, the way a user quits, and gates the exit status: a crash, a non-zero
exit, or a Quit still running 30 seconds later fails the run, so pass it whenever the run is being watched
by a tool. Without it the run ends with `SIGTERM`, which SDL turns into a quit event and Klip shuts down
from just as cleanly -- but nothing checks how it went. Klip's console output lands in `console.log` beside the recording, kept with it whenever a
gate fails. A sanitizer's own exit code would fail that gate on third-party leaks alone, so run them with
`exitcode=0` and let the triage scripts decide.

Two sources cannot be driven unattended. A **window** needs the grant kept -- tick *Remember the window*,
pick once by hand, and later runs restore it. A **region** needs a drag, and AT-SPI injects through XTEST,
which cannot reach a native Wayland surface. Running Klip under XWayland (`SDL_VIDEO_DRIVER=x11`) and
dragging with `xdotool` does not get round it on GNOME: its Xwayland runs with `-enable-ei-portal`, so
XTEST input is forwarded through the RemoteDesktop portal and is dropped without that portal's consent,
while X's own idea of the pointer moves as if it had worked.

Every gate is written to be able to fail, which is worth re-checking if one is ever changed: a gate that
delegates its verdict to another program's exit code is not a gate. `ffmpeg` returns 0 on a file whose
bitstream is destroyed, so the decode gate reads what it printed instead.

## Sanitizers

Configure a build with `KLIP_SANITIZER` and run the smoke test against it, passing `--quit`.

```bash
cmake --preset linux-gcc-asan && cmake --build --preset linux-gcc-asan
python3 scripts/smoke_test.py --klip build/gcc-asan/src/app/klip --quit
```

`address`, `undefined`, `address,undefined` and `thread` each have a preset for both compilers. The option instruments
everything compiled from source -- Klip, tge-core, SDL and Dear ImGui alike, as TSan needs both sides of a
handover to see it -- and for ASan and TSan forces `TGE_ENABLE_GLOBAL_ALLOCATOR` off, since rpmalloc hides allocations and
synchronisation from both. Run UBSan under **both** GCC and Clang: their check sets overlap but are not
identical, and one proves nothing about the other.

In an `address,undefined` build Clang has one runtime and one `log_path`, and `UBSAN_OPTIONS` overrides
`ASAN_OPTIONS`: name a log path in only one of them, or the ASan report lands in the UBSan file. GCC links
two runtimes and keeps them apart.

**Silence is only evidence if the instrumentation is there.** A sanitizer that was never linked reports
nothing, which reads exactly like a clean run. GCC leaves undefined references, so
`nm -uC <binary> | grep -c __ubsan_handle` must be non-zero; Clang links its runtime statically, so the
same count without `-u` is the one that matters there.

`scripts/asan_triage.py` and `scripts/tsan_triage.py` reduce a log to what belongs to this checkout --
Klip's `src/` and tge-core's own sources, which are built and instrumented with it, but not the libraries
tge-core vendors.
Entering Mesa, glib or libwayland puts Klip's frames on the stack of every error inside them, so ownership is
decided by which frame *performed* the access, not by which frames appear.

## Valgrind

```bash
cmake -S . -B build/valgrind -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTGE_ENABLE_GLOBAL_ALLOCATOR=OFF
valgrind --tool=memcheck --fullpath-after= --leak-check=full --log-file=vg.log <binary>
python3 scripts/valgrind_triage.py vg.log
```

`TGE_ENABLE_GLOBAL_ALLOCATOR=OFF` is not optional. tge-core routes `new` and `delete` through rpmalloc,
which takes its pages from `mmap`, and memcheck then watches an allocator it cannot look into and reports
almost nothing. Check with `nm -C <binary> | grep "T operator delete"`: it must print nothing. A
`KLIP_SANITIZER` build forces the flag off itself for ASan and TSan, which are blind to rpmalloc for the
same reason.

Memcheck is the only one of these with reach into FFmpeg, PipeWire, Mesa and libwayland: it instruments at run
time, so an access performed inside them is checked like any other. It is also cheaper here than its
reputation, because a damage-driven capture is paced by the compositor rather than by Klip.

## clang-tidy

```bash
cmake -S . -B build/clang -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build/clang $(find src -name '*.cpp')
```

The check list lives in `.clang-tidy`, and every exclusion there names the correct code it fires on. Turn
one back on before arguing with it; none are off because their findings were tedious.

This is the cheapest check and the only one that reads code which never runs -- the region path, the error
branches, everything a given machine's hardware never reaches.

## The floor

```bash
scripts/floor_build.sh
```

`CMakeLists.txt` refuses GCC below 14 and Clang below 19, and a floor is only real once something has been
built at it. The floor is Ubuntu 24.04's libraries -- FFmpeg 6.1.1, PipeWire 1.0 and the Wayland and X11
headers SDL builds against -- with the build tools C++26 needs on top: its own `g++-14` and `clang-19`, and
CMake 3.30, which the container takes from Kitware's release because 24.04 ships 3.28. The script builds
inside an `ubuntu:24.04` container rather than trusting whatever the host has moved on to. It installs
exactly the README's `apt install` line, so a dependency missing from that line fails here and not on a
reader's machine; then it runs `make` as the README says, and builds Debug and Release with both compilers.
`-Werror` is on, so a clean build is a result rather than an absence. It needs Docker and nothing else from
the host.

Give a compiler the libstdc++ that sits beside it, which is what a user on a stock distribution has.
Pairing a distribution Clang with a much newer GCC's libstdc++ instead fails in `bits/atomic_wait.h` on
`__builtin_popcountg`, a builtin that Clang does not have -- that is the pairing's fault, and it reads
exactly like a broken floor.

## The stock build

```bash
cp -a <checkout> /tmp/stock && rm -f /tmp/stock/CMakeUserPresets.json
cmake -S /tmp/stock -B /tmp/stock/b -G Ninja && cmake --build /tmp/stock/b
```

What a reader who downloaded the source actually does: a copy outside the checkout, no user presets, no
toolchain file, nothing pointed at a prefix. On the host it builds against the newest distribution
available, where the floor script builds against the oldest; both are needed, because a header the new
one supplies transitively can be missing from the old one, and an API the old one offers can be gone from
the new.

Run the smoke test against the binary it produces, not only the build. Compiling proves the headers agree;
it says nothing about whether that libavcodec still encodes what Klip asks it for.

**A machine without the dependencies stops at the first one missing and builds nothing, which reads like a
pass.**
Check the build reached a binary before believing it. And a transitive include is invisible until it is
absent: `qToBigEndian` compiled on every machine it was tried on, behind a header Qt 6.10 supplies and
Qt 6.4 does not, until a build against Qt 6.4 ran.

## The release package

```bash
scripts/build_release.sh && scripts/make_package.sh && scripts/check_package.sh
```

The build refuses its own result rather than shipping it: SDL without a backend it needs, an FFmpeg configured
with anything GPL, LGPLv3 or non-free or missing an encoder, muxer or filter Klip uses, a glibc symbol newer than
2.35, a NEEDED library that neither every PipeWire-1.0 distribution has nor the package carries, or a runpath that
is not `$ORIGIN`. The check then installs the package in clean containers, runs `ldd -r` on every file, which
resolves each symbol, and uninstalls it with both uninstallers.

**Building against a newer library than the oldest target runs is the drift to watch.** A call added in between
links fine and fails at load. The first package FFmpeg built against AlmaLinux 9's libva 2.22 called
`vaMapBuffer2`, which Ubuntu 24.04's libva 2.20 lacks; FFmpeg picks that call from the libva headers it compiles
against, so the build now compiles against libva 2.20's. PipeWire is the same shape -- 1.4 there, 1.0.5 on Ubuntu
24.04 -- with nothing to pin, and `ldd -r` in `ubuntu:24.04` is what would catch it, so that image stays in the
check.

A container has no portal and no graphics card, so nothing there records. Install the package and run the smoke
test against `~/.local/bin/klip`: that is the only check of the FFmpeg the package carries actually encoding.

## What none of them cover

FFmpeg, PipeWire, Mesa and the Wayland and X11 libraries are compiled elsewhere, so a sanitizer sees an
access inside them only if it intercepted the call. UBSan is the exception in the other direction: only
what this tree compiles -- Klip, tge-core, SDL and Dear ImGui -- carries its instrumentation, so anything it
reports is in code built here, and its silence is unambiguous.

The memory capture path -- `SubmitMapped`, `sws_scale` and the frame ring -- runs only where the
compositor offers no DMA-BUF. On hardware that offers one it is never exercised, by any of these.
