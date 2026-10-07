#!/usr/bin/env python3
#
# Records the screen with nothing clicked, then gates the file against what Klip said it wrote: codec,
# dimensions, duration and packet count. A zero-frame MP4 is a valid MP4, so a run that did not crash
# proves nothing on its own -- and neither does a file that decodes, since a buffer read with the wrong
# tiling decodes perfectly into noise. So a screenshot taken mid-recording is what the picture is held to.
#
# It films the whole desktop for as long as --seconds asks. The recording and the screenshot land in a
# temporary directory and are deleted unless a gate fails or --keep is given.
#
# Needs a screen cast grant -- the screen source keeps its restore token, so the portal picker appears
# only the first time -- python3-gi with the Atspi typelib, and ffprobe, from --ffprobe, $KLIP_FFPROBE or
# PATH. ffmpeg, found the same way, adds the decode and content checks on the card; without it both are
# skipped.

import argparse
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.parse import unquote, urlparse

REPO = Path(__file__).resolve().parent.parent
CONF = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "klip" / "Klip.conf"

DIRECTORY_SECTION = "output"
DIRECTORY_KEY = "directory"

CONTENT_WIDTH = 160
CONTENT_HEIGHT = 90
CONTENT_SPAN = 3.0
CONTENT_THRESHOLD = 0.8


def find_klip(explicit):
	if explicit:
		return Path(explicit)

	builds = sorted(REPO.glob("build/*/src/app/klip"), key=lambda path: path.stat().st_mtime, reverse=True)

	for wanted in ("RelWithDebInfo", "Debug"):
		for path in builds:
			if wanted in path.parts[-4]:
				return path

	return builds[0] if builds else None


def find_tool(name, explicit, variable):
	found = explicit or os.environ.get(variable) or shutil.which(name)

	return Path(found) if found else None


def read_conf_value(text, section, key, fallback):
	value = fallback
	current = None

	for line in text.splitlines():
		stripped = line.strip()

		if stripped.startswith("[") and stripped.endswith("]"):
			current = stripped[1:-1]
		elif current == section and stripped.startswith(key + "="):
			value = stripped[len(key) + 1:]

	return value


def write_conf_value(path, section, key, value):
	"""Sets one key in place and answers what was there before, or None when the key was absent."""
	previous = None
	current = None
	written = False
	out = []

	for line in path.read_text().splitlines():
		stripped = line.strip()

		if stripped.startswith("[") and stripped.endswith("]"):
			if current == section and not written:
				out.append(f"{key}={value}")
				written = True

			current = stripped[1:-1]

		if current == section and stripped.startswith(key + "="):
			previous = stripped[len(key) + 1:]

			if not written:
				out.append(f"{key}={value}")
				written = True

			continue

		out.append(line)

	if not written and current == section:
		out.append(f"{key}={value}")
	elif not written:
		out += ["", f"[{section}]", f"{key}={value}"]

	path.write_text("\n".join(out) + "\n")

	return previous


def remove_conf_key(path, section, key):
	current = None
	out = []

	for line in path.read_text().splitlines():
		stripped = line.strip()

		if stripped.startswith("[") and stripped.endswith("]"):
			current = stripped[1:-1]

		if not (current == section and stripped.startswith(key + "=")):
			out.append(line)

	path.write_text("\n".join(out) + "\n")


def find_button(atspi, node, label):
	"""Depth first, because the button is a leaf of the window that is a leaf of the application."""
	if node.get_role() == atspi.Role.PUSH_BUTTON and node.get_name() == label:
		return node

	for index in range(node.get_child_count()):
		child = node.get_child_at_index(index)
		hit = find_button(atspi, child, label) if child is not None else None

		if hit is not None:
			return hit

	return None


def press_record(process, timeout):
	import gi

	gi.require_version("Atspi", "2.0")
	from gi.repository import Atspi

	Atspi.init()
	deadline = time.monotonic() + timeout

	while time.monotonic() < deadline and process.poll() is None:
		desktop = Atspi.get_desktop(0)

		for index in range(desktop.get_child_count()):
			app = desktop.get_child_at_index(index)

			try:
				mine = app is not None and app.get_process_id() == process.pid
			except Exception:
				mine = False

			if mine:
				button = find_button(Atspi, app, "Record")

				if button is not None:
					Atspi.Action.do_action(button, 0)

					return True

		time.sleep(0.25)

	return False


def stop_via_tray(pid):
	"""Not through AT-SPI: the window is hidden while recording, which takes its tree with it."""
	service = f"org.kde.StatusNotifierItem-{pid}-1"
	call = subprocess.run(["gdbus", "call", "--session", "--dest", service, "--object-path",
	                       "/StatusNotifierItem", "--method",
	                       "org.kde.StatusNotifierItem.SecondaryActivate", "0", "0"],
	                      capture_output=True, text=True)

	return call.returncode == 0


def quit_via_tray(pid):
	"""Item 4 is Quit in Klip's own menu. SIGTERM runs none of the exit path, so a leak checker sees
	nothing and Terminate is never exercised."""
	service = f"org.kde.StatusNotifierItem-{pid}-1"
	call = subprocess.run(["gdbus", "call", "--session", "--dest", service, "--object-path", "/MenuBar",
	                       "--method", "com.canonical.dbusmenu.Event", "4", "clicked", "<0>", "0"],
	                      capture_output=True, text=True)

	return call.returncode == 0


def end_through_quit(process):
	"""Returns whether Klip ended cleanly through Quit, and how it ended; one still running afterwards is
	left for the caller's SIGTERM."""
	if process.poll() is not None:
		ending = (False, f"exited {process.returncode} before Quit was sent")
	elif not quit_via_tray(process.pid):
		ending = (False, "Quit was not accepted, so ended with SIGTERM")
	else:
		try:
			process.wait(timeout=30)
			ending = (process.returncode == 0, f"exited {process.returncode} through Quit")
		except subprocess.TimeoutExpired:
			ending = (False, "still running 30s after Quit, so ended with SIGTERM")

	return ending


def wait_for_recording(directory, timeout):
	deadline = time.monotonic() + timeout

	while time.monotonic() < deadline:
		written = sorted(directory.glob("klip-*"))

		if written:
			return written[0]

		time.sleep(0.1)

	return None


def wait_until_settled(path, timeout):
	deadline = time.monotonic() + timeout
	size = -1
	still_since = None

	while time.monotonic() < deadline:
		current = path.stat().st_size

		if current != size or current == 0:
			size = current
			still_since = None
		elif still_since is None:
			still_since = time.monotonic()
		elif time.monotonic() - still_since > 1.0:
			return True

		time.sleep(0.2)

	return False


def read_klip_log(path):
	text = path.read_text(errors="replace")
	device = re.search(r"VAAPI device (/dev/dri/renderD\d+)", text)
	size = re.search(r"Encoding (\d+)x(\d+)", text)
	frames = re.search(r"Wrote (\d+) frames", text)

	return {
		"device": device.group(1) if device else None,
		"width": int(size.group(1)) if size else None,
		"height": int(size.group(2)) if size else None,
		"frames": int(frames.group(1)) if frames else None,
	}


def probe(ffprobe, path):
	call = subprocess.run([str(ffprobe), "-v", "error", "-select_streams", "v:0", "-count_packets",
	                       "-show_entries",
	                       "stream=codec_name,width,height,nb_read_packets:format=duration",
	                       "-of", "json", str(path)], capture_output=True, text=True)

	if call.returncode != 0:
		return None

	report = json.loads(call.stdout)
	streams = report.get("streams", [])

	if not streams:
		return None

	stream = streams[0]

	return {
		"codec": stream.get("codec_name"),
		"width": int(stream.get("width", 0)),
		"height": int(stream.get("height", 0)),
		"packets": int(stream.get("nb_read_packets", 0)),
		"duration": float(report.get("format", {}).get("duration", 0.0)),
	}


def probe_audio(ffprobe, path):
	"""Answers what the audio track holds, or None when the file carries no audio at all."""
	call = subprocess.run([str(ffprobe), "-v", "error", "-select_streams", "a:0", "-count_packets",
	                       "-show_entries",
	                       "stream=codec_name,sample_rate,channels,nb_read_packets,duration",
	                       "-of", "json", str(path)], capture_output=True, text=True)

	if call.returncode != 0:
		return None

	streams = json.loads(call.stdout).get("streams", [])

	if not streams:
		return None

	stream = streams[0]

	return {
		"codec": stream.get("codec_name"),
		"rate": int(stream.get("sample_rate", 0)),
		"channels": int(stream.get("channels", 0)),
		"packets": int(stream.get("nb_read_packets", 0)),
		"duration": float(stream.get("duration", 0.0)),
	}


def decodes(ffmpeg, path, device, seconds):
	"""On the card, because an FFmpeg without libdav1d cannot decode the AV1 Klip just wrote.

	md5 rather than null: the null muxer rescales to the file's average frame rate, so a capture that
	arrives in bursts -- which is every damage-driven one -- is reported as non-monotonic. That is the
	muxer's arithmetic, not the recording, and it buried the output this gate actually reads.
	"""
	args = [str(ffmpeg), "-v", "error", "-hwaccel", "vaapi"]

	if device:
		args += ["-hwaccel_device", device]

	args += ["-t", str(seconds), "-i", str(path), "-f", "md5", "-"]
	call = subprocess.run(args, capture_output=True, text=True)
	complaints = [line for line in call.stderr.splitlines() if line.strip()]

	# A file whose frames are unreadable still exits 0 -- measured on a deliberately corrupted recording,
	# 98 lines of decoder errors and a returncode of zero. What it printed is the answer, not what it
	# returned, and -v error means anything printed is error level.
	return call.returncode == 0 and not complaints, complaints[0] if complaints else ""


def take_screenshot(directory):
	"""Answers (path, None) with the image moved into directory, or (None, why). The portal saves into the
	user's Pictures, so the file is moved out before anything else can fail."""
	import gi

	gi.require_version("Gio", "2.0")
	from gi.repository import Gio, GLib

	bus = Gio.bus_get_sync(Gio.BusType.SESSION)
	token = f"klip_smoke_{os.getpid()}"
	sender = bus.get_unique_name()[1:].replace(".", "_")
	handle = f"/org/freedesktop/portal/desktop/request/{sender}/{token}"
	loop = GLib.MainLoop()
	answer = {}

	def on_response(connection, sender_name, path, interface, signal_name, parameters):
		code, results = parameters.unpack()
		answer["code"] = code
		answer["uri"] = results.get("uri")
		loop.quit()

	# Subscribed before the call: the response can arrive before call_sync returns.
	subscription = bus.signal_subscribe("org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
	                                    "Response", handle, None, Gio.DBusSignalFlags.NO_MATCH_RULE,
	                                    on_response)

	try:
		bus.call_sync("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
		              "org.freedesktop.portal.Screenshot", "Screenshot",
		              GLib.Variant("(sa{sv})", ("", {"handle_token": GLib.Variant("s", token),
		                                             "interactive": GLib.Variant("b", False)})),
		              None, Gio.DBusCallFlags.NONE, -1, None)
		GLib.timeout_add_seconds(15, loop.quit)
		loop.run()
	except GLib.Error as error:
		answer["error"] = error.message
	finally:
		bus.signal_unsubscribe(subscription)

	reference = None
	reason = None

	if "error" in answer:
		reason = f"the Screenshot portal refused the call: {answer['error']}"
	elif answer.get("code") != 0 or not answer.get("uri"):
		reason = f"the Screenshot portal answered {answer.get('code', 'nothing within 15s')}"
	else:
		saved = Path(unquote(urlparse(answer["uri"]).path))
		reference = directory / "reference.png"
		shutil.move(saved, reference)

	return reference, reason


def image_size(ffprobe, path):
	call = subprocess.run([str(ffprobe), "-v", "error", "-show_entries", "stream=width,height", "-of",
	                       "csv=p=0", str(path)], capture_output=True, text=True)
	sizes = call.stdout.split(",")

	return (int(sizes[0]), int(sizes[1])) if call.returncode == 0 and len(sizes) == 2 else None


def thumbnails(ffmpeg, path, device, start, span):
	"""Grey CONTENT_WIDTH x CONTENT_HEIGHT frames, ten a second, decoded the way the decode gate does."""
	args = [str(ffmpeg), "-v", "error", "-hwaccel", "vaapi"]

	if device:
		args += ["-hwaccel_device", device]

	args += ["-ss", f"{start:.3f}", "-t", f"{span:.3f}", "-i", str(path), "-vf",
	         f"fps=10,scale={CONTENT_WIDTH}:{CONTENT_HEIGHT}:flags=area", "-f", "rawvideo",
	         "-pix_fmt", "gray", "-"]
	pixels = subprocess.run(args, capture_output=True).stdout
	size = CONTENT_WIDTH * CONTENT_HEIGHT

	return [pixels[offset:offset + size] for offset in range(0, len(pixels) - size + 1, size)]


def matches_screenshot(ffprobe, ffmpeg, recording, measured, device, reference, taken_at):
	"""Correlation rather than difference: the recording is limited range and the screenshot full, which
	shifts every level without changing the picture -- while scrambled tiles or a black frame share no
	structure with the screen at all. Best of the frames around the moment, since the desktop moves."""
	shot = image_size(ffprobe, reference)
	ratio = measured["width"] / measured["height"]
	detail = None
	passed = None

	if shot is None:
		detail = f"skipped: could not read {reference}"
	elif abs(shot[0] / shot[1] - ratio) > 0.01:
		detail = (f"skipped: the screenshot spans {shot[0]}x{shot[1]} and the recording "
		          f"{measured['width']}x{measured['height']}, so more than one monitor is in it")
	else:
		scaled = subprocess.run([str(ffmpeg), "-v", "error", "-i", str(reference), "-vf",
		                         f"scale={CONTENT_WIDTH}:{CONTENT_HEIGHT}:flags=area", "-f", "rawvideo",
		                         "-pix_fmt", "gray", "-"], capture_output=True).stdout
		start = max(0.0, taken_at - CONTENT_SPAN / 2)
		frames = thumbnails(ffmpeg, recording, device, start, CONTENT_SPAN)

		if len(scaled) != CONTENT_WIDTH * CONTENT_HEIGHT or statistics.pstdev(scaled) < 1.0:
			detail = "skipped: the screen is too uniform to tell a picture from a broken one"
		elif not frames:
			passed = False
			detail = f"no frames decoded between {start:.1f}s and {start + CONTENT_SPAN:.1f}s"
		else:
			best = max(statistics.correlation(frame, scaled) if statistics.pstdev(frame) > 0 else 0.0
			           for frame in frames)
			passed = best >= CONTENT_THRESHOLD
			detail = (f"r={best:.2f} against a screenshot at {taken_at:.1f}s, best of {len(frames)} "
			          f"frames, needs {CONTENT_THRESHOLD}")

	return passed, detail


def evaluate(ffprobe, ffmpeg, recording, claimed, codec, seconds, tolerance, wants_audio, reference):
	"""Answers one (name, passed, detail) per gate, or None when there is no video stream to read."""
	measured = probe(ffprobe, recording)

	if measured is None:
		return None

	gates = [("codec", measured["codec"] == codec, f"{measured['codec']}, configured {codec}")]

	sized = measured["width"] > 0 and measured["height"] > 0
	even = measured["width"] % 2 == 0 and measured["height"] % 2 == 0
	agrees = (claimed.get("width"), claimed.get("height")) == (measured["width"], measured["height"])
	gates.append(("dimensions", sized and even and agrees,
	              f"{measured['width']}x{measured['height']}, Klip said "
	              f"{claimed.get('width')}x{claimed.get('height')}"))

	slack = abs(measured["duration"] - seconds)
	gates.append(("duration", slack <= tolerance,
	              f"{measured['duration']:.2f}s for {seconds}s asked, tolerance {tolerance}s"))

	rate = measured["packets"] / measured["duration"] if measured["duration"] > 0 else 0.0
	gates.append(("packets", measured["packets"] > 0 and measured["packets"] == claimed.get("frames"),
	              f"{measured['packets']}, Klip wrote {claimed.get('frames')} ({rate:.1f}/s)"))

	audio = probe_audio(ffprobe, recording)

	if wants_audio:
		if audio is None:
			gates.append(("audio", False, "the settings asked for sound and the file carries none"))
		else:
			# A zero-packet audio track is a valid one, so the count is what proves sound arrived.
			sound = (audio["codec"] == "aac" and audio["rate"] == 48000 and audio["channels"] == 2
			         and audio["packets"] > 0)
			drift = abs(audio["duration"] - measured["duration"])
			gates.append(("audio", sound and drift <= tolerance,
			              f"{audio['codec']} {audio['rate']}Hz x{audio['channels']}, "
			              f"{audio['packets']} packets, {drift * 1000:.0f}ms off the video"))
	elif audio is not None:
		gates.append(("audio", False, "the settings asked for no sound and the file carries some"))

	window = min(2, seconds)

	if ffmpeg is None:
		gates.append(("decode", None, "skipped: no ffmpeg, pass --ffmpeg or set $KLIP_FFMPEG"))
	else:
		decoded, complaint = decodes(ffmpeg, recording, claimed.get("device"), window)
		gates.append(("decode", decoded, complaint or f"first {window}s on "
		                                              f"{claimed.get('device') or 'the default device'}"))

	shot, taken_at, why_not = reference

	if ffmpeg is None:
		gates.append(("content", None, "skipped: no ffmpeg, pass --ffmpeg or set $KLIP_FFMPEG"))
	elif shot is None:
		gates.append(("content", None, f"skipped: {why_not}"))
	else:
		passed, detail = matches_screenshot(ffprobe, ffmpeg, recording, measured, claimed.get("device"),
		                                    shot, taken_at)
		gates.append(("content", passed, detail))

	return gates


def run(args):
	klip = find_klip(args.klip)

	if klip is None or not klip.exists():
		print(f"no klip binary: build one, or pass --klip (looked under {REPO}/build)", file=sys.stderr)

		return 1

	ffprobe = find_tool("ffprobe", args.ffprobe, "KLIP_FFPROBE")

	if ffprobe is None:
		print("no ffprobe: install one, or pass --ffprobe / set $KLIP_FFPROBE", file=sys.stderr)

		return 1

	if not CONF.exists():
		print(f"no settings file at {CONF}: run Klip once first", file=sys.stderr)

		return 1

	ffmpeg = find_tool("ffmpeg", args.ffmpeg, "KLIP_FFMPEG")
	conf = CONF.read_text()
	codec = read_conf_value(conf, "output", "codec", "h264")
	container = read_conf_value(conf, "output", "container", "mp4")

	# What the settings ask for is what the file is held to: silence is only a pass when none was asked.
	wants_audio = (read_conf_value(conf, "audio", "systemEnabled", "false") == "true" or
	               read_conf_value(conf, "audio", "microphoneEnabled", "false") == "true")
	source = read_conf_value(conf, "capture", "source", "0")

	# A window is drivable only once its grant has been kept: without capture/rememberWindow the portal
	# raises a picker no automation can answer. A region always needs a human to drag one.
	remembers = read_conf_value(conf, "capture", "rememberWindow", "false") == "true"

	if source not in ("0", "1") or (source == "1" and not remembers):
		print(f"capture/source is {source}: the smoke test can drive the screen, and a window only with "
		      "capture/rememberWindow set after one manual pick", file=sys.stderr)

		return 1

	directory = Path(tempfile.mkdtemp(prefix="klip-smoke-"))
	logs_before = set(REPO.glob("logs/klip_*.log"))
	previous = write_conf_value(CONF, DIRECTORY_SECTION, DIRECTORY_KEY, str(directory))

	print(f"recording {'the whole screen' if source == '0' else 'the remembered window'} for "
	      f"{args.seconds}s as {codec} in {container}{' with sound' if wants_audio else ''}")

	process = None
	recording = None

	# A sanitizer or a crash writes here, and an exit status says nothing without the words behind it.
	output_path = directory / "console.log"
	output = open(output_path, "wb")
	reached_gates = False
	ending = None

	try:
		process = subprocess.Popen([str(klip)], cwd=REPO,
		                           env={**os.environ, "QT_LINUX_ACCESSIBILITY_ALWAYS_ON": "1"},
		                           stdout=output, stderr=subprocess.STDOUT)

		if not press_record(process, args.start_timeout):
			if process.poll() is not None:
				print("Klip exited at once: another instance already has the tray", file=sys.stderr)
			else:
				print("never found Klip's Record button over AT-SPI", file=sys.stderr)

			return 1

		recording = wait_for_recording(directory, args.start_timeout)

		if recording is None:
			print("Klip wrote no file: the portal grant may be waiting for an answer", file=sys.stderr)

			return 1

		appeared = time.monotonic()
		time.sleep(args.seconds / 2)

		if source == "0":
			asked = time.monotonic()
			shot, why_not = take_screenshot(directory)
			reference = (shot, (asked + time.monotonic()) / 2 - appeared, why_not)
		else:
			reference = (None, 0.0, "a window recording has no screenshot to compare against")

		time.sleep(max(0.0, args.seconds - (time.monotonic() - appeared)))

		if not stop_via_tray(process.pid):
			print("the tray would not take SecondaryActivate; the recording is still running",
			      file=sys.stderr)

			return 1

		if not wait_until_settled(recording, args.start_timeout):
			print("the file never stopped growing", file=sys.stderr)

			return 1

		reached_gates = True
	finally:
		if process is not None:
			if args.quit:
				ending = end_through_quit(process)

			if process.poll() is None:
				process.send_signal(signal.SIGTERM)
				process.wait(timeout=10)

		output.close()

		if not reached_gates:
			print(f"Klip's output is in {output_path}", file=sys.stderr)

		# Klip rewrites this file as it runs, so the restore edits what is there now, not a snapshot.
		if previous is None:
			remove_conf_key(CONF, DIRECTORY_SECTION, DIRECTORY_KEY)
		else:
			write_conf_value(CONF, DIRECTORY_SECTION, DIRECTORY_KEY, previous)

	logs_after = set(REPO.glob("logs/klip_*.log")) - logs_before
	claimed = read_klip_log(max(logs_after, key=lambda path: path.stat().st_mtime)) if logs_after else {}
	if not claimed:
		print("that binary wrote no log, so there is nothing to gate its own claims against: Release "
		      "builds compile logging out. Gate a debug or relwithdebinfo build instead.",
		      file=sys.stderr)

		return 1

	gates = evaluate(ffprobe, ffmpeg, recording, claimed, codec, args.seconds, args.tolerance,
	                 wants_audio, reference)

	if gates is None:
		print(f"ffprobe found no video stream in {recording}", file=sys.stderr)

		return 1

	# Only a run ended through Quit has an exit status worth gating: SIGTERM is how every other run ends.
	if args.quit:
		gates.append(("exit", *ending))

	for name, passed, detail in gates:
		mark = "SKIP" if passed is None else ("PASS" if passed else "FAIL")
		print(f"{mark} {name:<11} {detail}")

	failed = [name for name, passed, _ in gates if passed is False]

	print(f"{recording.stat().st_size / (1024 * 1024):.1f} MiB")

	if failed or args.keep:
		print(f"kept {recording}")
	else:
		shutil.rmtree(directory, ignore_errors=True)

	return 1 if failed else 0


def main():
	parser = argparse.ArgumentParser(description="Record the screen and prove the file holds it.")
	parser.add_argument("--seconds", type=float, default=5.0, help="how long to record (default 5)")
	parser.add_argument("--tolerance", type=float, default=1.5,
	                    help="seconds the duration may differ by (default 1.5)")
	parser.add_argument("--start-timeout", type=float, default=30.0,
	                    help="seconds to wait on the UI, the portal and the file (default 30)")
	parser.add_argument("--klip", help="the binary to drive (default: the newest under build/)")
	parser.add_argument("--ffprobe", help="ffprobe to gate with (default: $KLIP_FFPROBE, then PATH)")
	parser.add_argument("--ffmpeg",
	                    help="ffmpeg for the decode and content checks (default: $KLIP_FFMPEG, then PATH)")
	parser.add_argument("--quit", action="store_true",
	                    help="end through the tray's Quit rather than SIGTERM, so the exit path runs and "
	                         "its exit status is gated")
	parser.add_argument("--keep", action="store_true", help="keep the recording even when it passes")

	return run(parser.parse_args())


if __name__ == "__main__":
	sys.exit(main())
