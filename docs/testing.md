# Checking Klip

Klip's bugs are runtime ones. A compiler cannot see a frame arriving on the wrong thread, a buffer handed
back while the GPU still reads it, or a file that is a valid MP4 containing nothing. So the checks here
run the application and then ask whether the file holds a recording -- and each of them ends in a verdict
rather than a log.

The runtime ones need a screen cast grant, so they belong to whoever is at the machine and none can
gate CI. The last two need only a compiler.

## The smoke test

```bash
python3 scripts/smoke_test.py --seconds 15
```

Launches Klip, presses Record over AT-SPI, records, stops through the tray, and gates the file with
`ffprobe`: codec, dimensions against what Klip's own log claims, duration, packet count, the audio track,
and a decode on the card. Point it at binaries with `--ffprobe` / `--ffmpeg`, or `$KLIP_FFPROBE` /
`$KLIP_FFMPEG`, since a distribution FFmpeg may not decode what Klip writes.

`--quit` ends through the tray menu rather than `SIGTERM`. Without it no exit path runs at all, every
`Terminate` is skipped and a leak checker reports nothing, so pass it whenever the run is being watched by
a tool.

Two sources cannot be driven unattended. A **window** needs the grant kept -- tick *Remember the window*,
pick once by hand, and later runs restore it. A **region** needs a drag, and AT-SPI injects through XTEST,
which cannot reach a native Wayland surface.

Every gate is written to be able to fail, which is worth re-checking if one is ever changed: a gate that
delegates its verdict to another program's exit code is not a gate. `ffmpeg` returns 0 on a file whose
bitstream is destroyed, so the decode gate reads what it printed instead.

## Sanitizers

Configure a build with the flags and run the smoke test against it, passing `--quit`.

```bash
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```

`-fsanitize=thread` and `-fsanitize=undefined` the same way. Run UBSan under **both** GCC and Clang: their
check sets overlap but are not identical, and one proves nothing about the other.

**Silence is only evidence if the instrumentation is there.** A sanitizer that was never linked reports
nothing, which reads exactly like a clean run. GCC leaves undefined references, so
`nm -uC <binary> | grep -c __ubsan_handle` must be non-zero; Clang links its runtime statically, so the
same count without `-u` is the one that matters there.

`scripts/asan_triage.py` and `scripts/tsan_triage.py` reduce a log to what belongs to this checkout.
Entering Qt, Mesa or glib puts Klip's frames on the stack of every error inside them, so ownership is
decided by which frame *performed* the access, not by which frames appear.

## Valgrind

```bash
cmake -S . -B build/valgrind -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTGE_ENABLE_GLOBAL_ALLOCATOR=OFF
valgrind --tool=memcheck --leak-check=full --log-file=vg.log <binary>
python3 scripts/valgrind_triage.py vg.log
```

`TGE_ENABLE_GLOBAL_ALLOCATOR=OFF` is not optional. tge-core routes `new` and `delete` through rpmalloc,
which takes its pages from `mmap`, and memcheck then watches an allocator it cannot look into and reports
almost nothing. Check with `nm -C <binary> | grep "T operator delete"`: it must print nothing. The
sanitizers do not need the flag, because their own runtimes define those operators and the linker never
pulls tge-core's object out of the archive.

Memcheck is the only one of these with reach into FFmpeg, PipeWire, Qt and Mesa: it instruments at run
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

## The compiler floor

```bash
cmake -S . -B build/floor -G Ninja -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13
cmake --build build/floor
```

`CMakeLists.txt` refuses GCC below 13 and Clang below 18, and a floor is only real once something has been
built at it. Both compilers, at Debug and Release: each diagnoses what the other misses, and `-Werror` is
on, so a clean build is a result rather than an absence.

Give the system compiler the system libstdc++ that sits beside it, which is what a user on a stock
distribution has. Pairing a distribution Clang with a much newer GCC's libstdc++ instead fails in
`bits/atomic_wait.h` on `__builtin_popcountg`, a builtin that Clang does not have -- that is the pairing's
fault, and it reads exactly like a broken floor.

## The stock build

```bash
cp -a <checkout> /tmp/stock && rm -f /tmp/stock/CMakeUserPresets.json
cmake -S /tmp/stock -B /tmp/stock/b -G Ninja && cmake --build /tmp/stock/b
```

What a reader who downloaded the source actually does: a copy outside the checkout, no user presets, no
toolchain file, nothing pointed at a prefix. It wants the distribution's own Qt and FFmpeg rather than a
pinned build, because those are the versions `CMakeLists.txt` names as the floor -- on Ubuntu 24.04 that is
Qt 6.4.2 and FFmpeg 6.1.1, against a development build's 6.10 and 8.1.

Run the smoke test against the binary it produces, not only the build. Compiling proves the headers agree;
it says nothing about whether that libavcodec still encodes what Klip asks it for.

**A machine without the dependencies stops at `Qt6` and prints nothing further, which reads like a pass.**
Check the build reached a binary before believing it. And a transitive include is invisible until it is
absent: `qToBigEndian` compiled on every machine it was tried on, behind a header Qt 6.10 supplies and
Qt 6.4 does not, until this gate ran.

## What none of them cover

FFmpeg, PipeWire, Qt and Mesa are compiled elsewhere, so a sanitizer sees an access inside them only if it
intercepted the call. UBSan is the exception in the other direction: only Klip's own code carries its
instrumentation, so anything it reports is ours and its silence is unambiguous.

The memory capture path -- `SubmitMapped`, `sws_scale` and the frame ring -- runs only where the
compositor offers no DMA-BUF. On hardware that offers one it is never exercised, by any of these.
