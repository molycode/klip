# Traps

Things about screen capture on Linux that cost measurement to learn and that the code alone will not tell
you. Several exist to stop a plausible-looking change being made twice.

Measurements were taken on GNOME 46 with Mutter, a 3840x2160 panel, and an AMD Navi 32 discrete GPU beside
a Raphael iGPU. Where a number depends on that hardware, it says so.

## The portal and the stream

- **Every capture goes through `xdg-desktop-portal`**, including on X11. There is no direct screen grab,
  and a region is a monitor stream that Klip crops -- the portal has no region source type.
- **Reuse the portal `restore_token`** or the compositor's picker appears on every single Start.
- **A withdrawn cast arrives as `org.freedesktop.portal.Session.Closed`, not as a PipeWire state change.**
  The compositor's own indicator has a stop button; without that signal Klip records nothing and says
  nothing. Only the owning process may call `Close` on a session.
- **A frame rate ceiling goes in `SPA_FORMAT_VIDEO_maxFramerate`, not `SPA_FORMAT_VIDEO_framerate`.** A
  screen cast is a variable rate source, so `framerate` settles at 0/1 and carries no ceiling whatever
  range is offered for it. Measured: asking for a max of 30 through `framerate` alone came back negotiated
  at 59.97 and delivered 60.04 fps -- the compositor answered outside the offered range and looked like it
  was ignoring the cap entirely. Constraining `maxFramerate` instead negotiates 30.00, delivers 29.95, and
  halves the file.
- **A scaled display records at its logical size.** A 3840x2160 panel at 133% scale gives 2880x1620 from
  both the portal and the PipeWire format. Klip cannot ask Mutter for the panel's pixels through the
  portal, so do not report a resolution the stream is not carrying.
- **A screen that stops changing stops producing frames.** The portal sends on damage, so an idle desktop
  delivers nothing at all. Without holding the last frame until Stop, a 46 second recording ends up a 28
  second file that plays too fast.
- **GNOME's window picker offers a multiple choice it does not honour, and that is not Klip's bug.**
  Captured on the bus: `SelectSources` sends `multiple: false` and `types: 2`, which the specification
  defines as a single window, and xdg-desktop-portal-gnome 46.2 presents a picker that lets several be
  ticked anyway before returning one stream. The warning for more than one stream is already in place and
  has never fired; it is what will catch the day the backend starts returning two.

## Buffers and threads

- **A PipeWire buffer must be requeued immediately.** On the memory path, copy out on the PipeWire thread
  and hand the copy to the encoder thread. A DMA-BUF frame is the compositor's own memory and is only
  valid until the buffer goes back, so it is encoded inline on that thread instead -- there is nothing to
  hand over, and the import costs almost nothing.
- **Wait for the GPU before giving a DMA-BUF back.** `vaapi_vpp` submits the conversion and returns
  without syncing, so the compositor can draw the next frame into a buffer the GPU is still reading. It
  shows up as occasional frames carrying content from slightly later than their timestamp.
- **The copy path works and costs about five times the DMA-BUF one.** Forced onto it by offering the
  compositor no modifier form, a 2880x1620 capture spends 7.6 ms a frame on the encoder thread -- 6.2 of
  that in `sws_scale` converting BGRx to NV12 on the CPU, 1.3 uploading -- plus 1.4 ms of `memcpy` on the
  PipeWire thread, against 1.5 ms inline for DMA-BUF. It holds 30 fps with the encoder idle 68 ms between
  frames; it would not hold 60. Nothing reaches it without a source edit, so a regression there surfaces
  only on a compositor that hands out no DMA-BUF.

## Audio

- **`PW_KEY_TARGET_OBJECT` is a hint, not a binding.** With `AUTOCONNECT` alone a node that no longer
  exists does not fail: the stream connects to the *default* sink's monitor and delivers the wrong
  device's audio. `PW_STREAM_FLAG_DONT_RECONNECT` is what makes it real -- without it no "the device is
  gone" check can ever fire, and a stream that merely opened proves nothing.
- **Audio buffers carry no `SPA_META_Header`.** Video's PTS comes from `spa_meta_header.pts`; audio has
  only `pw_time.now`, which trails `CLOCK_MONOTONIC` by well under a millisecond. So the recording's epoch
  is the first video frame and audio is placed against it by counting samples. Beware reading drift from
  one buffer: a graph quantum change alone swings it by a full buffer, 21 ms measured, so the gap filler
  only reacts past 100 ms.
- **A sink monitor is a digital tap, ahead of the volume control.** It reads what applications play at the
  level they play it, so muting the speakers does not silence a recording. It also means silence is
  *exactly* zero, which is how a monitor is told apart from a microphone -- and why a correct meter looks
  dead when nothing is playing.

## Encoding

- **The quality ladder is measured, not derived.** `quality.hpp` holds `AVCodecContext::global_quality`
  values, which every VAAPI encoder reads once `rc_mode` is CQP: H.264 and HEVC clamp them to 1-51, AV1 reads
  a q_index and clamps to 1-255. They were measured on one AMD card so that a level costs about the same
  whichever codec it lands on -- against H.264 the HEVC column comes out 11-19% smaller and the AV1 column
  23-26%, which is the file size someone changing codec is really asking about. The bits per pixel per frame
  beside them, times ten thousand, come from the same card, and content moves them by half again either way,
  which is why the window says "up to".
- **WebM records AV1 only, and silent.** WebM carries VP8, VP9 and AV1, and Klip ships neither VP encoder.
  Its audio is Opus or Vorbis, which the pinned LGPL FFmpeg flags experimental; AAC, the one audio codec it
  gives MP4 and Matroska, WebM does not take.
- **A VAAPI surface belongs to the display it was made on.** `hwmap`'s `derive_device` mints a second
  VAAPI device beside the one Klip already opened, and an encoder bound to one display rejects the other's
  surfaces as an invalid id, then trips an assertion inside FFmpeg. The filter graph is handed Klip's own
  device instead, which is also two fewer open handles on the render node.
- **The first render node that opens is not the one with the most encoders.** Measured across two AMD
  GPUs in one machine: the Raphael iGPU on `renderD129` encodes H.264 and HEVC but refuses AV1 -- "No
  usable encoding entrypoint found for profile VAProfileAV1Profile0 (32)" -- while the Navi 32 on
  `renderD128` takes all three. So Klip opens every encoder on every node that gives a VAAPI device and
  records on the richest, which costs about 8 ms per extra node at startup.

- **VAAPI hands packets back with no duration, and MP4 needs the last one's.** The edit list ends at the
  last packet's pts plus its duration, so with none it ends where the last frame starts and every player
  drops that frame -- one packet short in `ffprobe`, gone entirely when the frame before it is a keyframe,
  since the demuxer stops indexing at the first keyframe that reaches the end. So each video packet waits
  for the next to give it a duration, and the last takes the stop time. Re-encoding the last frame at the
  stop time instead only moves the problem onto the copy.

## The region selector

- **Nothing composites behind a fullscreen window.** Measured on Mutter: where the region selector painted
  nothing at all, a capture read pure black, not the desktop. The toolkit was never at fault -- the surface
  really is ARGB. So the selector paints an `org.freedesktop.portal.Screenshot` image as its backdrop instead of
  showing through, and that is a second portal permission on top of the ScreenCast one.
- **A plain window cannot stand in for a fullscreen one.** Wayland gives a client no say in its position:
  a window sized to the screen was placed at the work area origin -- 68,32 on a desktop with a dock and a
  top bar -- so it neither covers the output nor maps 1:1 to stream pixels.
- **A window hidden a moment ago is still on screen.** GNOME animates it away over about 150 ms, so a
  screenshot taken straight after the hide still holds it: the whole window, all 630k pixels of it. It
  cuts both ways -- a capture that opens the moment the selector closes records the selector fading out,
  ghost backdrop and rectangle and all, for its first seven frames. Hide before the stream opens rather
  than once it is running, and settle after every hide that a capture or a screenshot follows.
- **An unfocused fullscreen window opens under the others.** Mutter lifts a fullscreen window above the
  rest only while it has focus, and Wayland gives SDL no always-on-top. Shown with neither an activation
  token nor a recent click behind it, the selector covered the top bar and the dock -- dimmed by exactly
  its shade, 0.571 -- while a terminal and Files stayed above it untouched. A toggle from Klip's own button
  carries the click and a tray click can carry a token; a toggle sent over D-Bus by a script carries
  neither, so an unattended run cannot show the selector in front.
- **A window resized mid-recording is recorded at one to one, on purpose.** The valid area is read from
  `SPA_META_VideoCrop` on the first frame and fixed there, so a window that grows is clipped and one that
  shrinks carries the compositor's black padding -- measured against a window cycling 700x600, 1000x800
  and 420x360: the frame stays 722x660 throughout, a one pixel checkerboard survives every size with 14415
  sharp alternations, and the padding is solid black in 3705 of 3705 sampled pixels. Nothing is ever
  resampled, which for text and thin strokes is the whole point. Mutter does not renegotiate the stream on
  resize; only the crop changes, and its origin is always +0,0 because a window renders at the buffer's
  corner. Centred bars would need `pad_vaapi`, which on Mesa's radeonsi fills green whatever colour is
  asked for.

## The window

- **SDL 3.4 prefers XWayland on a compositor without `fifo-v1`**, GNOME 46 among them, so Klip asks for
  `wayland,x11` in that order. The log's `Display:` line names the driver that won.
- **SDL keeps the screen awake by default** -- `SDL_HINT_VIDEO_ALLOW_SCREENSAVER` starts off -- so without
  the hint a Klip sitting in the tray would keep the screen from locking.
- **An activation token is spent only by a show, and only from SDL's own copy of the environment.** SDL
  copies the environment at startup and reads `XDG_ACTIVATION_TOKEN` from that copy, so it is set with
  `SDL_SetEnvironmentVariable(SDL_GetEnvironment(), …)`, not `setenv`. Raising a window that is already
  mapped asks the compositor for a fresh token from the focused surface, which GNOME refuses while Klip has
  no focus -- so a shown window is hidden and shown again to spend one. And `SDL_OpenURL` hands any token
  still set to the program it starts, so none is ever left set.
- **Hiding a Wayland window destroys its toplevel**, and the next show makes a new one, which is why the
  hide and show above activates it.
- **A resize is asynchronous on Wayland.** `SDL_SetWindowSize` sends a request the compositor answers later,
  so the size read straight after it is still the old one; the window asks only when the height it needs
  changes, never once per frame.

## The tray

- **GNOME's panel never shows the idle tray label.** Ubuntu's AppIndicator extension comments
  `XAyatanaLabel` and `XAyatanaNewLabel` out of the interface XML its proxy is built from, and GDBus drops
  a property absent from that interface when it fills its cache, so the label reads null until an
  `XAyatanaNewLabel` arrives -- which first happens when a recording starts. Announcing it alone after
  attach, and announcing it with the `NewIcon`/`NewTitle`/`NewToolTip` burst, were both tried and neither
  helped; the shell issued no follow-up read for `NewIcon` either. Do not retry either approach without
  new evidence.
- **A left click on the tray icon opens the menu ~400 ms late, and that is the better bargain.** The shell
  cannot tell a single click from the first half of a double click, so it arms a timer for
  `Clutter.Settings.doubleClickTime`. GNOME takes that branch solely because Klip implements `Activate`;
  an item without it gets an instant menu and no double-click gesture. Right click is unambiguous and
  opens immediately.

- **Locking the screen takes the tray icon away for a second or two, and that is not Klip.** GNOME
  disables the AppIndicator extension on the lock screen and re-enables it on unlock, when it re-scans the
  bus by introspection and finds the item again. Seen on GNOME 50 with the sd-bus tray; nothing in Klip
  needs to re-register.

## Building

- **Ubuntu 24.04's default Clang cannot use its `std::expected`.** libstdc++ 13 declares it only when
  `__cpp_concepts` is at least 202002L, and Clang 18 reports 201907L, so the floor's Clang build stops at
  "no template named 'expected'" while GCC 13 is fine. The same archive ships `clang-19`, which is the
  floor, and configure refuses 18 by name.

- **SDL leaves out a backend whose development files are missing, without a word.** The build succeeds and
  the window then has no title bar on GNOME (no libdecor), runs under XWayland (no Wayland) or never shows
  a folder dialog (no D-Bus). `cmake/klip_sdl.cmake` reads SDL's own `HAVE_*` results back from its
  directory and stops with the package names instead.

- **`sd_bus_error` cannot be forward-declared before libsystemd 259.** Until then sd-bus declares it as a
  typedef of an anonymous struct, so `struct sd_bus_error;` is a redefinition with a different type -- on
  the floor's 255, never on this host's 259. A header that names it includes `<systemd/sd-bus.h>`; the
  opaque `sd_bus`, `sd_bus_message` and `sd_bus_slot` forward-declare fine everywhere.

## Verifying

- **Never conclude a recording worked because no crash occurred.** A zero-frame MP4 is a valid MP4. Gate
  on `ffprobe`: codec, dimensions, duration and packet count. `scripts/smoke_test.py` does that
  unattended and counts packets rather than frames -- `-count_frames` needs a decoder, and an FFmpeg
  without libdav1d reads every AV1 file Klip writes as zero frames. See [testing.md](testing.md).
- **A recording that decodes cleanly can still be noise.** After the move to Ubuntu 26.04, every frame
  came out as a grid of scrambled tiles -- Klip read the settled modifier as LINEAR while the compositor
  had rendered tiled -- and codec, dimensions, duration, packets and decode all passed. Only looking at
  the picture catches it, which is what the smoke test's content gate is for.
