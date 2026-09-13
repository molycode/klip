#!/usr/bin/env python3
#
# Reads an AddressSanitizer log and answers the only question that matters: how much of it is ours.
# Hard errors first, then leaks grouped by the frame that allocated them. A leak whose innermost frame
# is a third-party library is that library's cache, not a bug here -- Qt's D-Bus type registry and
# fontconfig's substitution tables both live for the process and account for most of a run's total.

import argparse
import collections
import re
import sys
from pathlib import Path

# Qt and other vendors ship their own build paths in frames, and theirs contain "/src/" too, so
# ownership is decided by this checkout's root rather than by a fragment.
REPO = Path(__file__).resolve().parent.parent

FRAME = re.compile(r"#\d+ 0x\S+ in ([^\n]+)")
LEAK = re.compile(r"(Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object")


def innermost(record):
	"""The allocating frame, skipping the sanitizer's own interceptors."""
	for frame in FRAME.findall(record):
		if "libsanitizer" not in frame and "asan_new_delete" not in frame and "asan_malloc" not in frame:
			return frame.strip()

	return ""


def main():
	parser = argparse.ArgumentParser(description="Summarise an AddressSanitizer log")
	parser.add_argument("log", help="the file named by ASAN_OPTIONS log_path")
	parser.add_argument("--own", default=f"{REPO}/src/",
	                    help="path prefix marking first-party frames (default: this checkout's src)")
	args = parser.parse_args()

	try:
		with open(args.log, errors="replace") as handle:
			text = handle.read()
	except OSError as error:
		sys.exit(f"cannot read {args.log}: {error}")

	errors = collections.Counter(re.findall(r"ERROR: AddressSanitizer: ([a-z\-]+)", text))
	hard = {kind: n for kind, n in errors.items() if kind != "detected"}

	print("== errors ==")
	if hard:
		for kind, n in sorted(hard.items(), key=lambda kv: -kv[1]):
			print(f"  {n:4}  {kind}")
	else:
		print("  none")

	own_leaks = []
	other = collections.Counter()
	other_bytes = collections.Counter()

	for record in re.split(r"\n(?=(?:Direct|Indirect) leak of )", text):
		match = LEAK.match(record)

		if match is None:
			continue

		nbytes = int(match.group(2))
		frame = innermost(record)

		if args.own in frame:
			own_leaks.append((nbytes, int(match.group(3)), frame))
		else:
			name = frame.split(" /")[0].split("(")[0].strip() or "unknown"
			other[name[:58]] += 1
			other_bytes[name[:58]] += nbytes

	print(f"\n== leaks allocated by this project ({args.own}) ==")
	if own_leaks:
		for nbytes, count, frame in sorted(own_leaks, reverse=True):
			print(f"  {nbytes:9,} B in {count:4} object(s)   {frame}")
	else:
		print("  none")

	print("\n== leaks allocated inside third-party libraries ==")
	for name, count in other.most_common(12):
		print(f"  {count:4} record(s)  {other_bytes[name]:9,} B   {name}")

	summary = re.search(r"SUMMARY: AddressSanitizer: .*", text)
	print(f"\n{summary.group(0) if summary else 'no SUMMARY line'}")

	return 1 if hard or own_leaks else 0


if __name__ == "__main__":
	sys.exit(main())
