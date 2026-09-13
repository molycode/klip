#!/usr/bin/env python3
#
# Reads a ThreadSanitizer log and separates races this project performs from races it merely appears
# above. The distinction matters: entering a driver or a toolkit puts our frames on the stack of every
# race inside it, and those are not ours to fix. Only frames #0 and #1 of an access stack performed it.

import argparse
import collections
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ACCESS = re.compile(r"\s*(?:Atomic )?(?:Write|Read|Previous write|Previous read|Previous atomic)")


def performing_frames(report):
	"""The frames that actually touched the memory, not the callers that led there."""
	frames = []

	for block in re.split(r"\n(?=\s*(?:Atomic )?(?:Write|Read|Previous))", report):
		if not ACCESS.match(block):
			continue

		for number, frame in re.findall(r"#(\d+) ([^\n]+)", block):
			if int(number) <= 1:
				frames.append(frame)

	return frames


def main():
	parser = argparse.ArgumentParser(description="Summarise a ThreadSanitizer log")
	parser.add_argument("log", help="the file named by TSAN_OPTIONS log_path")
	parser.add_argument("--own", default=f"{REPO}/src/",
	                    help="path prefix marking first-party frames (default: this checkout's src)")
	args = parser.parse_args()

	try:
		with open(args.log, errors="replace") as handle:
			text = handle.read()
	except OSError as error:
		sys.exit(f"cannot read {args.log}: {error}")

	ours = []
	foreign = collections.Counter()
	kinds = collections.Counter()

	for report in re.split(r"\n(?=WARNING: ThreadSanitizer:)", text):
		if not report.startswith("WARNING: ThreadSanitizer:"):
			continue

		kind = re.match(r"WARNING: ThreadSanitizer: ([a-z\- ]+)", report)
		kinds[kind.group(1).strip() if kind else "unknown"] += 1

		performed = [f for f in performing_frames(report) if args.own in f]

		if performed:
			threads = re.findall(r"Thread T\d+ '([^']+)'", report)
			ours.append((performed, threads))
		else:
			summary = re.search(r"SUMMARY: ThreadSanitizer: [^(]*\(([^)+]+)", report)
			foreign[summary.group(1).split("/")[-1] if summary else "unknown"] += 1

	print("== report kinds ==")
	for kind, n in kinds.most_common():
		print(f"  {n:4}  {kind}")

	print(f"\n== races performed by this project ({args.own}) ==")
	if ours:
		for frames, threads in ours:
			for frame in dict.fromkeys(frames):
				print("  " + frame.split(" (")[0].strip())
			if threads:
				print(f"    threads: {', '.join(dict.fromkeys(threads))}")
			print()
	else:
		print("  none")

	print("== races performed inside third-party libraries ==")
	for lib, n in foreign.most_common(15):
		print(f"  {n:4}  {lib}")

	return 1 if ours else 0


if __name__ == "__main__":
	sys.exit(main())
