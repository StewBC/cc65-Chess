#!/usr/bin/env python3
"""Compare two cc65-Chess UCI builds over the full opening book.

This is the cheap pre-gate for search changes.  Both engines search every
position in book.epd at the shipped budgets; the report says whether a change
buys enough nodes and completed depth to be detectable by the match gauntlet.
"""

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


TESTS = Path(__file__).resolve().parent
INFO_RE = re.compile(
	r"^info depth (\d+) score (cp|mate) (-?\d+) nodes (\d+) pv (\S+)"
)


@dataclass(frozen=True)
class Result:
	depth: int
	score_kind: str
	score: int
	nodes: int
	pv: str
	bestmove: str


def csv_ints(text):
	return [int(item) for item in text.split(",") if item.strip()]


def option_commands(options):
	commands = []
	for option in options:
		if "=" not in option:
			raise ValueError(f"option must be NAME=VALUE, got {option!r}")
		name, value = option.split("=", 1)
		commands.append(f"setoption name {name} value {value}")
	return commands


def run_engine(path, options, levels, fens):
	commands = ["uci"] + option_commands(options)
	for level in levels:
		commands.append(f"setoption name Skill value {level}")
		for fen in fens:
			commands.extend((f"position fen {fen}", "go"))
	commands.append("quit")

	completed = subprocess.run(
		[str(path)], input="\n".join(commands) + "\n", text=True,
		capture_output=True, timeout=300,
	)
	if completed.returncode:
		sys.exit(
			f"{path}: exited {completed.returncode}\n"
			f"{completed.stdout[-1000:]}{completed.stderr[-1000:]}"
		)

	results = []
	pending = None
	for line in completed.stdout.splitlines():
		match = INFO_RE.match(line)
		if match:
			pending = match
		elif line.startswith("bestmove "):
			if pending is None:
				sys.exit(f"{path}: bestmove without preceding info: {line}")
			results.append(Result(
				depth=int(pending.group(1)),
				score_kind=pending.group(2),
				score=int(pending.group(3)),
				nodes=int(pending.group(4)),
				pv=pending.group(5),
				bestmove=line.split()[1],
			))
			pending = None

	expected = len(levels) * len(fens)
	if len(results) != expected:
		sys.exit(f"{path}: parsed {len(results)} searches, expected {expected}")
	return results


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--baseline", required=True, type=Path)
	parser.add_argument("--candidate", required=True, type=Path)
	parser.add_argument("--book", type=Path, default=TESTS / "book.epd")
	parser.add_argument("--levels", default="1,2,3,4")
	parser.add_argument("--target-levels", default="1,2,3,4",
	                    help="levels required to clear the threshold")
	parser.add_argument("--threshold", type=float, default=20.0,
	                    help="minimum node saving percentage")
	parser.add_argument("--baseline-option", action="append", default=[])
	parser.add_argument("--candidate-option", action="append", default=[])
	parser.add_argument("--exact", action="store_true",
	                    help="require every search result to be identical")
	parser.add_argument("--report-only", action="store_true",
	                    help="report without enforcing the threshold")
	args = parser.parse_args()

	levels = csv_ints(args.levels)
	targets = set(csv_ints(args.target_levels))
	if not levels or any(level < 1 or level > 4 for level in levels):
		parser.error("levels must be drawn from 1,2,3,4")
	if not targets.issubset(set(levels)):
		parser.error("target levels must be included in --levels")
	for path in (args.baseline, args.candidate, args.book):
		if not path.is_file():
			parser.error(f"not a file: {path}")

	fens = [line.strip() for line in args.book.read_text().splitlines() if line.strip()]
	baseline = run_engine(args.baseline, args.baseline_option, levels, fens)
	candidate = run_engine(args.candidate, args.candidate_option, levels, fens)

	print(f"{len(fens)} positions at each shipped budget")
	print("level   baseline  candidate   saving   depth b/c   deeper  shallower  moves  gate")
	print("-" * 86)
	failed = []
	for index, level in enumerate(levels):
		start = index * len(fens)
		stop = start + len(fens)
		base = baseline[start:stop]
		cand = candidate[start:stop]
		base_nodes = sum(result.nodes for result in base)
		cand_nodes = sum(result.nodes for result in cand)
		saving = 100.0 * (base_nodes - cand_nodes) / base_nodes
		base_depth = sum(result.depth for result in base)
		cand_depth = sum(result.depth for result in cand)
		deeper = sum(c.depth > b.depth for b, c in zip(base, cand))
		shallower = sum(c.depth < b.depth for b, c in zip(base, cand))
		moves = sum(c.bestmove != b.bestmove for b, c in zip(base, cand))
		gate = "--"
		if args.exact:
			gate = "same" if base == cand else "DIFF"
		elif level in targets:
			gate = "pass" if saving >= args.threshold else "STOP"
			if saving < args.threshold:
				failed.append(level)
		print(
			f"{level:>5} {base_nodes:>10} {cand_nodes:>10} {saving:>+7.2f}%"
			f" {base_depth:>5}/{cand_depth:<5} {deeper:>7} {shallower:>10}"
			f" {moves:>6}  {gate}"
		)

	if args.exact:
		if baseline != candidate:
			for index, (base, cand) in enumerate(zip(baseline, candidate)):
				if base != cand:
					level = levels[index // len(fens)]
					position = index % len(fens) + 1
					print(f"exact: FAIL at level {level}, position {position}")
					print(f"  baseline:  {base}")
					print(f"  candidate: {cand}")
					break
			return 1
		print(f"exact: pass, all {len(baseline)} searches identical")
		return 0

	if failed and not args.report_only:
		print(
			f"pre-gate: STOP, levels {','.join(map(str, failed))} are below "
			f"the {args.threshold:g}% detectability threshold"
		)
		return 1
	print("pre-gate: pass" if not failed else "pre-gate: report only")
	return 0


if __name__ == "__main__":
	sys.exit(main())
