#!/usr/bin/env python3
#
# Reads a valgrind log and separates what this project did from what it merely called into. Entering a
# driver or a toolkit puts our frames above every error inside it, and valgrind's own malloc replacement
# sits above every allocation, so neither the first frame nor the presence of a frame settles ownership.

import argparse
import collections
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# tge-core is built from source and instrumented with Klip, so what it does is ours to fix -- but not the
# libraries it vendors under its own external/.
OWN = (f"{REPO}/src/", f"{REPO}/external/tge-core/public/", f"{REPO}/external/tge-core/private/")
KIND = re.compile(r"^(Invalid read|Invalid write|Invalid free|Mismatched free|Conditional jump|"
                  r"Use of uninitialised|Syscall param|Source and destination overlap|"
                  r"[\d,]+ (?:\([\d,]+ direct, [\d,]+ indirect\) )?bytes in [\d,]+ blocks are "
                  r"(?:definitely|indirectly) lost)")


def blocks(text):
	"""Valgrind writes one blank-separated block per error, each line prefixed with its pid."""
	current = []

	for line in (re.sub(r"^==\d+== ?", "", l) for l in text.split("\n")):
		if line.strip():
			current.append(line)
		elif current:
			yield current
			current = []

	if current:
		yield current


def performer(frames):
	"""The innermost frame that is not valgrind's own replacement: the access itself, or the caller that
	allocated. Valgrind prints only a file's basename unless run with --fullpath-after=."""
	return next((f for f in frames if "vg_replace_" not in f), "")


def owned(frame, prefixes):
	return any(prefix in frame for prefix in prefixes)


def describe(prefixes):
	return ", ".join(prefix.removeprefix(f"{REPO}/") for prefix in prefixes)


def main():
	parser = argparse.ArgumentParser(description="Summarise a valgrind log")
	parser.add_argument("log", help="the file named by --log-file")
	parser.add_argument("--own", action="append",
	                    help="a path prefix marking first-party frames, repeatable (default: this checkout's "
	                         "src and tge-core's own sources)")
	args = parser.parse_args()
	own = args.own or OWN

	try:
		with open(args.log, errors="replace") as handle:
			text = handle.read()
	except OSError as error:
		sys.exit(f"cannot read {args.log}: {error}")

	ours = []
	foreign = collections.Counter()
	kinds = collections.Counter()

	for block in blocks(text):
		match = KIND.match(block[0])

		if match is None:
			continue

		kind = match.group(1)
		kind = "definitely lost" if "definitely lost" in kind else (
			"indirectly lost" if "indirectly lost" in kind else kind)
		kinds[kind] += 1

		frames = [l.strip() for l in block if l.strip().startswith(("at ", "by "))]
		real = performer(frames)

		if owned(real, own):
			ours.append((kind, block[0][:66], [f for f in frames if "vg_replace_" not in f][:3]))
		else:
			name = re.search(r"\(in ([^)]+)\)|: ([^(]+) \(", real)
			foreign[(kind, (name.group(1) or name.group(2)).split("/")[-1].strip() if name else "unknown")] += 1

	print("== findings ==")
	for kind, n in kinds.most_common():
		print(f"  {n:4}  {kind}")

	print(f"\n== involving this project ({describe(own)}) ==")
	if ours:
		for kind, header, frames in ours:
			print(f"  [{kind}] {header}")
			for frame in frames:
				print("      " + frame[:104])
			print()
	else:
		print("  none")

	print("== entirely in third-party code ==")
	for (kind, lib), n in foreign.most_common(12):
		print(f"  {n:4}  {kind:22} {lib}")

	summary = re.search(r"ERROR SUMMARY: .*", text)
	print(f"\n{summary.group(0) if summary else 'no ERROR SUMMARY line'}")

	return 1 if ours else 0


if __name__ == "__main__":
	sys.exit(main())
