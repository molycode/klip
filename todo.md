# TODO

Open work. Each item states what is known, what is only predicted, and what would settle it.

## Klip has only ever run on one machine

Everything in this file was found on a single setup: GNOME Shell 46 on Wayland with Mutter and
`xdg-desktop-portal-gnome`, an AMD Navi 32 discrete card beside a Raphael iGPU, and a 3840x2160 panel at
133% scale that streams 2880x1620 and offers nothing above 60 Hz, on Ubuntu 24.04. Klip has never been
started anywhere else. That is the reason the version is 0.1.0 rather than 1.0.0, and it is worth knowing
before any number below is trusted.

Most of it should carry, for a structural reason rather than a hopeful one: every capture goes through
`xdg-desktop-portal`, so the compositor sits behind an interface instead of being compiled against. That is
a prediction. The parts most likely to disagree, in order:

**A different portal backend.** The restore token, the window picker, and whether a DMA-BUF is offered at
all were each observed against `xdg-desktop-portal-gnome` alone. KDE's backend and wlroots' answer the same
calls and are under no obligation to answer them the same way. Where no DMA-BUF is offered Klip falls to
the memory path, which this machine reaches only by editing the source.

**A different GPU.** `encode/public/encode/quality.hpp` is a table of VAAPI `global_quality` constants
measured on that one AMD card. An Intel or NVIDIA user gets a quality ladder calibrated for hardware they
do not own -- the encoder probe at startup drops whatever their card refuses, so the format list will be
theirs while the numbers behind it are not.

**An X11 session.** The portal is used there too and there is no separate path, so this ought to be the
dullest of the three. "Ought to" is the entire evidence: Klip has never been started outside Wayland.

**What would settle it:** one recording on KDE or a wlroots compositor, one on Intel or NVIDIA VAAPI, and
one in an X11 session. Each is a `scripts/smoke_test.py` run and a look at which gates pass; none of them
needs new code.

## Audio records, but its drift has only been watched for forty seconds

Klip records system audio, a microphone, or both mixed into one AAC track in MP4 and Matroska. The
capture is a PipeWire stream Klip opens itself -- there is no audio portal, and none is needed for a
native build. What a sandboxed build would do is still open.

Measured while building it, so none of this needs establishing again:

**The monitor tap is pre-volume and pre-mute.** A captured tone came back at the source file's own level
(-45.1 dBFS) with the sink at 0.72, and bit-identically with the sink muted. A recording carries what
applications play, whatever the speakers are set to.

**`PW_KEY_TARGET_OBJECT` is only a hint.** With `AUTOCONNECT` alone a node that no longer exists does not
fail: the stream silently connects to the *default* sink's monitor and delivers audio from the wrong
device. `PW_STREAM_FLAG_DONT_RECONNECT` is what makes the binding real -- without it the "refuse to start
when the device is gone" check can never fire.

**Audio buffers carry no `SPA_META_Header`.** Video takes its PTS from `spa_meta_header.pts`; audio has to
use `pw_time.now`, which trails `CLOCK_MONOTONIC` by 130-670 us. So the encoder's epoch comes from the
first video frame and audio is placed against it by counting samples.

**A graph quantum change costs one buffer of apparent drift.** Idle, buffers are a flat 1024 frames; the
moment another client plays, they arrive at 1024..2048 and the drift reading swings by one buffer -- 21 ms
measured -- while the sample count stays exact. That is why the gap filler only reacts past 100 ms: a
one-frame threshold would inject silence that is not missing.

**What would settle it:** a recording long enough to separate crystal drift from that noise. Everything
measured so far sits inside one buffer and says nothing about an hour: eight seconds showed 8 ms, and
forty seconds showed 7 to 18 ms across four runs, which is noise rather than a trend.
10 ppm is 36 ms per hour and can be ignored; 50 ppm is 180 ms and cannot. The encoder logs the figure at
the end of every recording, so the answer needs one thirty-minute capture and a reading, not new code.
`swr_set_compensation` is the fix if it comes to that, and all fifo filling goes through one function so
it stays a one-function change.

**Also unmeasured: the constant A/V offset.** Two offsets stack and neither has been measured. A monitor
tap reads samples the sink has not played yet, so audio runs early by one sink latency -- `pw_time.delay`
is what would correct it. AAC's encoder priming is the other, and it is already correct: the first packet
carries a pts 1024 samples below the first input, which both muxers compensate for. Do not clamp it. A
clap-and-flash pair measured with ffprobe is what would settle the pair of them.

**WebM still records silent.** It carries only Opus or Vorbis and the pinned FFmpeg flags both encoders
experimental, so the UI disables the audio group for it and the encoder refuses AAC into it. Doing it
properly means `--enable-libopus` in `scripts/build_ffmpeg.sh` and libopus as a dependency -- BSD, so no
conflict with the LGPL/MIT stance, but a real addition.

## Nothing here has ever recorded above 60 fps

The frame rate ceiling is a range offered to the compositor, so uncapped means whatever it composites at.
The encoder is told the negotiated `max_framerate` and the window's estimate asks
`QScreen::refreshRate()`, so both track the real rate rather than assuming 60 -- but neither has been
watched doing it, because this display cannot go above 60 anywhere near a usable resolution.

Measured from `org.gnome.Mutter.DisplayConfig`: every mode from 1368x768 up to 3840x2160 tops out at 60,
and the only modes offering more are 1280x1024@75.025, 1152x864@75, 1024x768@75.029 and 800x600@75. The
current mode is 2880x1620@59.975 at scale 1.0, and `QScreen::refreshRate()` reports 59.974 there, which
rounds to the 60 that used to be hardcoded. So on this machine the change is provably a no-op, which is
exactly why it proves nothing.

**What would settle it:** a monitor that runs above 60 at a sane resolution, or a deliberate drop to
1280x1024@75.025 for one recording -- checking that the stream negotiates ~75, that the encoder is handed
75 rather than 60, and that the delivered rate follows. Not worth scrambling a desktop for on its own;
worth doing the first time this runs on high refresh hardware.

## The size estimate is measured and shown, but not yet remembered

The line under Quality multiplies a bits-per-pixel table measured on this hardware by a frame rate and a
size. It used to say "up to about N MiB per minute", which read as a ceiling. It is not one, and it cannot
be: `rc_mode` is CQP, so the encoder holds quality fixed and lets the bitrate follow the content. File size
is an output, not a setting -- the same reason you cannot know how large a JPEG will be before seeing the
picture.

Measured on a 103 second window capture of a game at Best quality, H.264, uncapped: the file came out at
60 MiB per minute where the line said 17.4, and the encoder spent **661** bits per pixel where the table
says **78**. That is 8.5x, on content the table was never calibrated for -- it was measured on a desktop,
which is mostly static, while a game repaints every pixel of every frame in the dark gradients and texture
that cost the most.

**Measured and shown, now.** A recording displays what it is actually costing: bytes so far, MiB per
minute, and what an hour comes to at that rate, in the window and on the tray. The rate is deliberately in
the same unit the hint predicts in, so the gap between 17.4 promised and 60 delivered is visible rather
than silent. Two content-dependent unknowns sit behind that gap and neither is about resolution: how many
bits a frame costs under CQP, and how many frames the compositor sends at all, since the frame rate is a
ceiling and a still screen sends far fewer.

**What is left is remembering it.** Klip knows bytes, duration, frame count, codec and quality at the end
of every recording, so persisting bits per pixel per frame keyed on (codec, quality) -- 15 cells -- would
replace the seeded table with the user's own content. Store it with the sample count beside it, so the
line can stop hedging once it is speaking from measurement rather than from a table; an exponential
average keeps it honest for somebody who switches from filming a desktop to filming games.

That belongs in `$XDG_STATE_HOME/klip`, not in `Klip.conf`. It is derived measurement rather than
preference: nobody sets it, deleting it should cost nothing but a recalibration, and it must never sync
between machines, because the figure folds in the GPU's encoder as well as the content.

**What would settle it:** the persistence and the fallback. The measuring half exists.

## A scaled display can only be recorded at its logical size

Measured: this 3840x2160 panel at 133% scale streams 2880x1620, and the PipeWire format offers nothing
larger. The portal has no option for the panel's own pixels, so a 4K screen records at 1620p and there is
currently nothing Klip can do about it from inside the portal's API.

**What would settle it:** whether Mutter exposes a full-resolution screen cast at all -- the private
org.gnome.Mutter.ScreenCast interface takes explicit stream sizes, but using it would tie Klip to GNOME and
abandon the portal, which is the opposite of the direction the rest of the capture path takes.

## The region rectangle assumes Qt and the stream share a coordinate space

Measured here: Qt reports the screen as 2880x1620 with a device pixel ratio of 1, which is exactly what
PipeWire streams, so a rectangle dragged in the selector indexes stream pixels directly. Nothing enforces
that. A second monitor, or a setup where Qt's logical geometry differs from the stream size, would crop
the wrong area with no warning.

**What would settle it:** scaling the rectangle by stream size over screen geometry once the stream is
running, which needs the selector's result to survive until after the portal answers.

## tge-core's CLog does real work in a constructor, and Klip has three of them

`CLog::CLog` registers with the log system, takes a mutex and inserts into a map, so an application-lifetime
global like `Klip::gLog` allocates during static initialisation, where a failure cannot be caught. clang-tidy
reports it as `bugprone-throwing-static-initialization` on two of them, and it is right: tge-core's own rule
is that a constructor default-initialises members and nothing else, precisely so that globals are safe.

It is two rather than three because `KlipEncode` links no Qt, so it never re-enables exceptions and keeps
the global `-fno-exceptions`, under which the constructor cannot throw at all. The check is silent there on
a build flag, not on a difference in the code -- all three loggers are the same declaration.

The static initialisation *order* is fine -- `GetLogSystem()` is a function-local static, constructed on
first use -- and the practical risk is small, since a failed allocation that early ends the process anyway.
What is not fine is that the pattern tge-core documents and the pattern `CLog` uses disagree.

**What would settle it:** moving the registration out of the constructor, which is a change to tge-core and
touches every project that logs. The check stays on here so it is not forgotten.

## The public headers are Linux-bound, and Windows is coming

`capture/public/` exports `pipewire_stream.hpp` and `portal_session.hpp` by name, `frame.hpp` carries
`EFrameMemory::DmaBuf`, and `encode/public/encode/quality.hpp` is a table of VAAPI `global_quality`
constants measured on one AMD card. None of that is wrong today -- it is what Klip is -- but it is all
sitting in the half of the tree a second backend would have to share.

The comments say so plainly, on purpose: they are the map of what has to split, not a liability to hide.

**What would settle it:** the port itself. The first question it asks is whether `SFrame` can describe a
D3D11 texture without naming one, and the second is what a quality ladder means when the encoder is NVENC.
