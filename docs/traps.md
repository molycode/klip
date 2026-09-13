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

- **A VAAPI surface belongs to the display it was made on.** `hwmap`'s `derive_device` mints a second
  VAAPI device beside the one Klip already opened, and an encoder bound to one display rejects the other's
  surfaces as an invalid id, then trips an assertion inside FFmpeg. The filter graph is handed Klip's own
  device instead, which is also two fewer open handles on the render node.
- **The first render node that opens is not the one with the most encoders.** Measured across two AMD
  GPUs in one machine: the Raphael iGPU on `renderD129` encodes H.264 and HEVC but refuses AV1 -- "No
  usable encoding entrypoint found for profile VAProfileAV1Profile0 (32)" -- while the Navi 32 on
  `renderD128` takes all three. So Klip opens every encoder on every node that gives a VAAPI device and
  records on the richest, which costs about 8 ms per extra node at startup.

## The region selector

- **Nothing composites behind a fullscreen window.** Measured on Mutter: where the region selector painted
  nothing at all, a capture read pure black, not the desktop. Qt was never at fault -- the surface really
  is ARGB. So the selector paints an `org.freedesktop.portal.Screenshot` image as its backdrop instead of
  showing through, and that is a second portal permission on top of the ScreenCast one.
- **A plain window cannot stand in for a fullscreen one.** Wayland gives a client no say in its position:
  a window sized to the screen was placed at the work area origin -- 68,32 on a desktop with a dock and a
  top bar -- so it neither covers the output nor maps 1:1 to stream pixels.
- **A window hidden a moment ago is still on screen.** GNOME animates it away over about 150 ms, so a
  screenshot taken straight after `hide()` still holds it: the whole window, all 630k pixels of it. It
  cuts both ways -- a capture that opens the moment the selector closes records the selector fading out,
  ghost backdrop and rectangle and all, for its first seven frames. Hide before the stream opens rather
  than once it is running, and settle after every hide that a capture or a screenshot follows.
- **A window resized mid-recording is recorded at one to one, on purpose.** The valid area is read from
  `SPA_META_VideoCrop` on the first frame and fixed there, so a window that grows is clipped and one that
  shrinks carries the compositor's black padding -- measured against a window cycling 700x600, 1000x800
  and 420x360: the frame stays 722x660 throughout, a one pixel checkerboard survives every size with 14415
  sharp alternations, and the padding is solid black in 3705 of 3705 sampled pixels. Nothing is ever
  resampled, which for text and thin strokes is the whole point. Mutter does not renegotiate the stream on
  resize; only the crop changes, and its origin is always +0,0 because a window renders at the buffer's
  corner. Centred bars would need `pad_vaapi`, which on Mesa's radeonsi fills green whatever colour is
  asked for.

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

## Building

- **A C++ standard CMake does not know sinks `find_package(Qt6)`, not the compile.** `CMAKE_CXX_STANDARD`
  propagates into Qt's own `try_compile` probes, so declaring 26 made `Qt6Core` report itself NOT_FOUND
  under every CMake below 4.x -- the visible error naming `Qt6/FindWrapAtomic.cmake` and reading as a Qt
  problem. It is not one: `Target "cmTC_…" requires the language dialect "CXX26"` is the line underneath.
  Raising the standard therefore raises the CMake floor with it, which is why Klip asks for 23 and not for
  whatever is newest.

## Verifying

- **Never conclude a recording worked because no crash occurred.** A zero-frame MP4 is a valid MP4. Gate
  on `ffprobe`: codec, dimensions, duration and packet count. `scripts/smoke_test.py` does that
  unattended and counts packets rather than frames -- `-count_frames` needs a decoder, and an FFmpeg
  without libdav1d reads every AV1 file Klip writes as zero frames. See [testing.md](testing.md).
