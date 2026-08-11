#!/usr/bin/env python3
"""Run cc65-Chess UCI against Apple II Sargon II under windowed a2m-v2.

See sargon/README.md for the emulator choreography, screen-format quirks,
calibration procedure, artifact layout, and recovery notes.

The repository carries the known-good 143,360-byte DOS 3.3 image beside this
script as Sargon-trimmed.dsk.  The harness validates it and mounts a disposable
working copy; it never depends on or modifies an external source disk.

Boot sequence for the compilation disk is deliberately exact:

    R, H, wait through the transient copy-disk screen, G RETURN

R and H are single-key catalog commands.  RETURN while the loader is still on
the transient screen drops into the monitor, so all later input is gated on
the text-page contents.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

import chess
import chess.pgn


REPO = Path(__file__).resolve().parents[1]
A2M_REPO = REPO.parent / "a2m-v2"
A2M_BIN = A2M_REPO / "build" / "a2m-v2"
A2M_TOOLS = A2M_REPO / "tools"
UCI_BIN = REPO / "tests" / "uci"
SARGON_DISK = Path(__file__).with_name("Sargon-trimmed.dsk")
WORK_DISK = Path("/private/tmp/cc65-sargon-match.dsk")
DOS33_SIZE = 35 * 16 * 256
DEFAULT_PORT = 6511
MAX_PLIES = 300
# How long Sargon is given for one move.  120s is ample at level 1; the deeper
# levels are a different animal - the manual has level 6 searching several ply
# and "automatically more deeply" in the opening and the endgame, and a search
# that overruns this is scored as a bridge failure and retried rather than
# waited for, which loses the game rather than the move.  --move-timeout
MOVE_TIMEOUT = 120.0
SCREEN_TIMEOUT = 60.0
KEY_DELAY = 0.025
CC65_SKILLS = {
	1: ("Very Easy", 400),
	2: ("Easy", 1200),
	3: ("Harder", 15000),
	4: ("Very Hard", 60000),
}

sys.path.insert(0, str(A2M_TOOLS))
from a2m_control_client import Ctl  # noqa: E402


COORD_RE = re.compile(r"^[A-H][1-8][-X][A-H][1-8][+#]?$", re.IGNORECASE)
# Sargon stores a two-character decimal move number without a hundreds digit.
# At 100 the tens character therefore advances from "9" to ":", then ";" at
# 110, through "?" at 150 (our 300-ply safety cap).
MOVE_NUMBER_RE = re.compile(r"^\s*([0-9:;<=>?]{1,2})\s")


class BridgeError(RuntimeError):
	pass


def decode_move_number(token: str) -> int:
	value = 0
	for char in token:
		digit = ord(char) - ord("0")
		if not 0 <= digit <= 15:
			raise BridgeError(f"bad Sargon move-number character {char!r}")
		value = value * 10 + digit
	return value


@dataclass
class GameResult:
	index: int
	cc_color: str
	result: str
	termination: str
	plies: int
	duration: float
	moves: list[str]
	opening: str

	@property
	def cc_score(self) -> float:
		if self.result == "1/2-1/2":
			return 0.5
		cc_won = (self.result == "1-0" and self.cc_color == "white") or (
			self.result == "0-1" and self.cc_color == "black"
		)
		return 1.0 if cc_won else 0.0


def log(message: str) -> None:
	stamp = time.strftime("%Y-%m-%d %H:%M:%S")
	print(f"[{stamp}] {message}", flush=True)


def prepare_disk() -> None:
	if not SARGON_DISK.is_file():
		# the image is deliberately not in the repository - see sargon/README.md
		raise BridgeError(
			f"Sargon disk image not found at {SARGON_DISK}.  It is not shipped "
			f"with this repository; sargon/README.md gives the exact size and "
			f"SHA-256 the harness expects."
		)
	if SARGON_DISK.stat().st_size != DOS33_SIZE:
		raise BridgeError(
			f"trimmed Sargon disk is {SARGON_DISK.stat().st_size} bytes; "
			f"expected exactly {DOS33_SIZE}"
		)
	# Mount a disposable working copy in case an emulator or guest ever writes
	# to the disk.  The repository fixture remains the authoritative input.
	shutil.copyfile(SARGON_DISK, WORK_DISK)
	log(f"prepared Sargon working disk from repository fixture: {WORK_DISK}")


class UCIEngine:
	def __init__(self, skill: int = 1, own_book: bool = True):
		self.skill = skill
		self.own_book = own_book
		self.book_seed = 1
		self.proc = subprocess.Popen(
			[str(UCI_BIN)],
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
			bufsize=1,
		)
		self._send("uci")
		self._read_until("uciok")
		self._send(f"setoption name Skill value {skill}")
		self._send("isready")
		self._read_until("readyok")

	def set_book_seed(self, seed: int) -> None:
		"""The game seeds its randomiser from a free-running counter; this is
		that byte.  It picks which table entry comes up and, for the first few
		searches, perturbs move ordering among exact ties - both of which the
		shipping front end does and neither of which UCI does by default."""
		self.book_seed = seed & 0xFF

	def _send(self, line: str) -> None:
		if self.proc.stdin is None:
			raise BridgeError("UCI stdin is closed")
		self.proc.stdin.write(line + "\n")
		self.proc.stdin.flush()

	def _read_until(self, prefix: str) -> str:
		if self.proc.stdout is None:
			raise BridgeError("UCI stdout is closed")
		for line in self.proc.stdout:
			line = line.strip()
			if line.startswith(prefix):
				return line
		raise BridgeError(f"UCI engine exited while waiting for {prefix!r}")

	def new_game(self) -> None:
		self._send("ucinewgame")
		self._send(f"setoption name Skill value {self.skill}")
		self._send(f"setoption name OwnBook value {'true' if self.own_book else 'false'}")
		self._send(f"setoption name BookSeed value {self.book_seed}")
		self._send("isready")
		self._read_until("readyok")

	def best_move(self, moves: Iterable[str]) -> str:
		move_text = " ".join(moves)
		cmd = "position startpos"
		if move_text:
			cmd += " moves " + move_text
		self._send(cmd)
		self._send("go")
		line = self._read_until("bestmove ")
		move = line.split()[1]
		if move == "0000":
			raise BridgeError("cc65 UCI returned no move in a live position")
		return move

	def close(self) -> None:
		if self.proc.poll() is None:
			try:
				self._send("quit")
			except (BrokenPipeError, BridgeError):
				pass
			try:
				self.proc.wait(timeout=3)
			except subprocess.TimeoutExpired:
				self.proc.terminate()


class SargonEmulator:
	def __init__(self, port: int):
		self.port = port
		self.proc: Optional[subprocess.Popen] = None
		self.ctl: Optional[Ctl] = None

	def start(self) -> None:
		if self.proc is not None:
			self.close()
		self.proc = subprocess.Popen(
			[
				str(A2M_BIN),
				"--noini",
				"--disk",
				str(WORK_DISK),
				"--control-port",
				str(self.port),
			],
			cwd=A2M_REPO,
			stdout=subprocess.DEVNULL,
			stderr=subprocess.DEVNULL,
		)
		deadline = time.monotonic() + 20
		while time.monotonic() < deadline:
			if self.proc.poll() is not None:
				raise BridgeError(f"a2m-v2 exited during launch: {self.proc.returncode}")
			try:
				self.ctl = Ctl(port=self.port, timeout=60)
				break
			except (ConnectionRefusedError, OSError):
				time.sleep(0.1)
		else:
			raise BridgeError("timed out connecting to a2m-v2 control port")
		self.wait_text("SARGON ][", SCREEN_TIMEOUT)
		self.key("R")
		time.sleep(0.15)
		self.key("H")
		self.wait_text("NEW GAME", SCREEN_TIMEOUT)
		self._ok("set-turbo max")
		log("Sargon booted; control reset returns to this reusable New Game state")

	def _ok(self, command: str) -> str:
		if self.ctl is None:
			raise BridgeError("emulator control client is not connected")
		result = self.ctl.cmd(command)
		if result[0] != "ok":
			raise BridgeError(f"{command!r} failed: {result}")
		return result[1]

	def screen(self) -> list[str]:
		if self.ctl is None:
			raise BridgeError("emulator control client is not connected")
		memory = self.ctl.mem(0x400, 0x400)
		lines = []
		for row in range(24):
			address = 0x400 + (row % 8) * 0x80 + (row // 8) * 0x28
			chars = []
			for value in memory[address - 0x400 : address - 0x400 + 40]:
				code = value & 0x7F
				if code < 0x20:
					code += 0x40
				chars.append(chr(code))
			lines.append("".join(chars))
		return lines

	def wait_text(self, text: str, timeout: float) -> list[str]:
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			lines = self.screen()
			if text in "\n".join(lines):
				return lines
			time.sleep(0.05)
		raise BridgeError(f"timed out waiting for screen text {text!r}")

	def wait_state(self, wanted: str, timeout: float) -> str:
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			state = self._ok("get-state")
			if f"state={wanted}" in state:
				return state
			time.sleep(0.025)
		raise BridgeError(f"timed out waiting for emulator state {wanted!r}")

	def key(self, char: str) -> None:
		if len(char) != 1:
			raise ValueError("key() requires one character")
		self._ok(f"key {ord(char)}")

	def type_text(self, text: str, delay: float = KEY_DELAY) -> None:
		for char in text:
			self.key(char)
			time.sleep(delay)

	def restore_ready(self) -> None:
		self._ok("reset")
		self.wait_text("NEW GAME", 5)
		self._ok("set-turbo max")

	def begin_game(
		self, cc_color: chess.Color, level: int, seed: int
	) -> tuple[tuple[int, str], ...]:
		self.restore_ready()
		self.type_text("G\r")
		self.wait_text("YOUR COLOR", 5)
		user_color = "W" if cc_color == chess.WHITE else "B"
		self.type_text(user_color + "\r")
		self.wait_text("LEVEL OF PLAY", 5)
		before = self._move_entries(self.screen(), not cc_color)
		# Sargon's keyboard wait loop increments zero-page $4E.  That byte is
		# the program's entropy source for its randomized opening book, but a
		# scripted reset otherwise lands on the same value every game.  Pause so
		# the loop cannot race the write, seed it explicitly, queue the level and
		# RETURN, then resume execution.
		self._ok("pause")
		self.wait_state("paused", 5)
		if self.ctl is None:
			raise BridgeError("emulator control client is not connected")
		self.ctl.set_mem(0x004E, bytes([seed & 0xFF]))
		self.type_text(str(level) + "\r")
		self._ok("run")
		if cc_color == chess.WHITE:
			deadline = time.monotonic() + 5
			while time.monotonic() < deadline:
				if self._player_cursor(self.screen(), cc_color):
					break
				time.sleep(0.025)
			else:
				raise BridgeError("timed out waiting for cc65 White move prompt")
		return before

	@staticmethod
	def _move_entries(lines: list[str], sargon_color: chess.Color) -> tuple[tuple[int, str], ...]:
		entries = []
		for line in lines:
			match = MOVE_NUMBER_RE.match(line[:10])
			if not match:
				continue
			move_no = decode_move_number(match.group(1))
			cell = line[10:18] if sargon_color == chess.WHITE else line[23:31]
			token = cell.strip().replace("O", "0")
			if token and token != "`":
				entries.append((move_no, token))
		return tuple(entries)

	@staticmethod
	def _player_cursor(lines: list[str], cc_color: chess.Color) -> bool:
		for line in lines:
			match = MOVE_NUMBER_RE.match(line[:10])
			if not match:
				continue
			cell = line[10:18] if cc_color == chess.WHITE else line[23:31]
			if "`" in cell:
				return True
		return False

	@classmethod
	def _new_move_tokens(
		cls,
		entries: tuple[tuple[int, str], ...],
		before: tuple[tuple[int, str], ...],
	) -> list[str]:
		return [
			token
			for entry in entries
			if entry not in before
			for token in (entry[1],)
			if cls._looks_like_sargon_move(token)
		]

	def sargon_move(self, board: chess.Board, before: tuple[tuple[int, str], ...]) -> chess.Move:
		deadline = time.monotonic() + MOVE_TIMEOUT
		last_screen = None
		while time.monotonic() < deadline:
			lines = self.screen()
			entries = self._move_entries(lines, board.turn)
			joined = "\n".join(lines)
			terminal_display = "CHECKMATE" in joined or "STALEMATE" in joined
			new_moves = self._new_move_tokens(entries, before)
			if (
				new_moves
				and (self._player_cursor(lines, not board.turn) or terminal_display)
			):
				# A forecast such as "MATE IN 1" can occupy the following row
				# after Sargon has already written its coordinate move and returned
				# the cursor.  Select the newest valid added move, not simply the
				# final nonblank cell.
				return self._parse_sargon_move(board, new_moves[-1])
			last_screen = lines
			time.sleep(0.025)
		nonblank = [line.rstrip() for line in (last_screen or []) if line.strip()]
		raise BridgeError(f"timed out waiting for Sargon move; screen={nonblank!r}")

	@staticmethod
	def _looks_like_sargon_move(token: str) -> bool:
		clean = token.upper().replace("O", "0").strip()[:5].rstrip("+#").strip()
		return bool(
			COORD_RE.match(clean)
			or clean in ("0-0", "0-0-0", "PXPEP")
		)

	@staticmethod
	def _parse_sargon_move(board: chess.Board, token: str) -> chess.Move:
		# A move cell begins with a five-character coordinate/castle/EP token.
		# Sargon writes CHECK or CHECKMATE immediately after it, and the fixed
		# column slice may contain any prefix of that annotation (" CH", etc.).
		clean = token.upper().replace("O", "0").strip()[:5].rstrip("+#").strip()
		legal = list(board.legal_moves)
		if COORD_RE.match(clean):
			uci4 = clean.replace("-", "").replace("X", "").lower()
			matches = [move for move in legal if move.uci()[:4] == uci4]
			if len(matches) == 1:
				return matches[0]
			if matches:
				queens = [move for move in matches if move.promotion == chess.QUEEN]
				if len(queens) == 1:
					return queens[0]
		elif clean in ("0-0", "0-0-0"):
			king_side = clean == "0-0"
			matches = [
				move
				for move in legal
				if board.is_castling(move)
				and (board.is_kingside_castling(move) == king_side)
			]
			if len(matches) == 1:
				return matches[0]
		elif clean == "PXPEP":
			matches = [move for move in legal if board.is_en_passant(move)]
			if len(matches) == 1:
				return matches[0]
			raise BridgeError(
				f"ambiguous en passant {token!r}: {[move.uci() for move in matches]}"
			)
		raise BridgeError(
			f"cannot translate Sargon token {token!r}; legal={[move.uci() for move in legal]}"
		)

	def player_move(self, move: chess.Move) -> None:
		if move.promotion not in (None, chess.QUEEN):
			raise BridgeError(f"Sargon cannot be given cc65 underpromotion {move.uci()}")
		name = move.uci()[:2].upper() + "-" + move.uci()[2:4].upper()
		self.type_text(name + "\r")

	def close(self) -> None:
		if self.ctl is not None:
			try:
				self.ctl.cmd("quit-client")
			except Exception:
				pass
			try:
				self.ctl.s.close()
			except Exception:
				pass
			self.ctl = None
		if self.proc is not None and self.proc.poll() is None:
			self.proc.terminate()
			try:
				self.proc.wait(timeout=5)
			except subprocess.TimeoutExpired:
				self.proc.kill()
		self.proc = None


def outcome_for(board: chess.Board, plies: int) -> Optional[tuple[str, str]]:
	outcome = board.outcome(claim_draw=True)
	if outcome is not None:
		return outcome.result(), outcome.termination.name.lower()
	if plies >= MAX_PLIES:
		return "1/2-1/2", f"{MAX_PLIES}-ply safety cap"
	return None


def seed_for(batch: str, game_index: int, attempt: int, paired: bool = True) -> int:
	# Give each colour-swapped pair the same seed, then advance the low bits by
	# one for the next pair.  Sargon's opening picker only consumes a few low
	# bits, so a large odd stride can look varied as a byte while repeating the
	# exact same book choice for every game of one colour.
	# paired is the alternating-colour default.  A single-colour run has no pair
	# to hold a seed constant for, and giving both games of a "pair" the same
	# seed there would halve the openings the run ever sees
	step = (game_index // 2) if paired else game_index
	value = (step + (attempt - 1) * 17 + sum(map(ord, batch))) & 0xFF
	return value or 1


def play_game(
	emulator: SargonEmulator,
	engine: UCIEngine,
	batch: str,
	index: int,
	sargon_level: int,
	attempt: int,
	cc_force_color: Optional[chess.Color] = None,
) -> GameResult:
	started = time.monotonic()
	if cc_force_color is None:
		cc_color = chess.WHITE if index % 2 == 0 else chess.BLACK
	else:
		cc_color = cc_force_color
	board = chess.Board()
	moves: list[str] = []
	game_seed = seed_for(batch, index, attempt, paired=cc_force_color is None)
	engine.set_book_seed(game_seed)
	engine.new_game()
	pending_sargon_before = emulator.begin_game(
		cc_color, sargon_level, game_seed
	)

	while True:
		terminal = outcome_for(board, len(moves))
		if terminal is not None:
			result, termination = terminal
			break

		cc_to_move = board.turn == cc_color
		if cc_to_move:
			# At max Sargon may finish its reply before the next Python statement.
			# Snapshot its move cells before typing the cc65 move, never afterward.
			pending_sargon_before = emulator._move_entries(
				emulator.screen(), not cc_color
			)
			# The opening tables used to be written out again here in Python,
			# because tests/uci called the search directly and could not reach
			# cpu.c at all.  It can now, so the harness asks the engine and the
			# thing being measured is the thing that ships - which matters more
			# for the black table than it ever did for the white one, since a
			# Python copy of a table is a copy of what it was believed to be.
			uci = engine.best_move(moves)
			try:
				move = chess.Move.from_uci(uci)
			except ValueError as exc:
				raise BridgeError(f"bad cc65 UCI move {uci!r}") from exc
			if move not in board.legal_moves:
				raise BridgeError(f"cc65 produced illegal move {uci} in {board.fen()}")
			emulator.player_move(move)
			board.push(move)
			moves.append(move.uci())
		else:
			if pending_sargon_before is None:
				raise BridgeError("missing pre-move Sargon screen signature")
			move = emulator.sargon_move(board, pending_sargon_before)
			pending_sargon_before = None
			board.push(move)
			moves.append(move.uci())

	opening = " ".join(moves[:6])
	return GameResult(
		index=index,
		cc_color="white" if cc_color == chess.WHITE else "black",
		result=result,
		termination=termination,
		plies=len(moves),
		duration=time.monotonic() - started,
		moves=moves,
		opening=opening,
	)


def append_pgn(
	path: Path,
	game_result: GameResult,
	batch: str,
	level: int,
	cc_skill: int,
) -> None:
	game = chess.pgn.Game()
	cc_name, nodes = CC65_SKILLS[cc_skill]
	cc_player = f"cc65-Chess {cc_name} ({nodes} nodes)"
	game.headers["Event"] = f"cc65-Chess vs Sargon II {batch}"
	game.headers["Site"] = "a2m-v2"
	game.headers["Date"] = time.strftime("%Y.%m.%d")
	game.headers["Round"] = str(game_result.index + 1)
	if game_result.cc_color == "white":
		game.headers["White"] = cc_player
		game.headers["Black"] = f"Sargon II Level {level}"
	else:
		game.headers["White"] = f"Sargon II Level {level}"
		game.headers["Black"] = cc_player
	game.headers["Result"] = game_result.result
	game.headers["Termination"] = game_result.termination
	node = game
	for name in game_result.moves:
		node = node.add_variation(chess.Move.from_uci(name))
	with path.open("a", encoding="utf-8") as handle:
		print(game, file=handle, end="\n\n")


CSV_FIELDS = [
	"index",
	"cc_color",
	"result",
	"cc_score",
	"termination",
	"plies",
	"duration_seconds",
	"opening_6plies",
	"moves",
]


def load_completed(csv_path: Path) -> list[dict[str, str]]:
	if not csv_path.is_file():
		return []
	with csv_path.open(newline="", encoding="utf-8") as handle:
		return list(csv.DictReader(handle))


def append_csv(csv_path: Path, result: GameResult) -> None:
	exists = csv_path.is_file() and csv_path.stat().st_size > 0
	with csv_path.open("a", newline="", encoding="utf-8") as handle:
		writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
		if not exists:
			writer.writeheader()
		writer.writerow(
			{
				"index": result.index,
				"cc_color": result.cc_color,
				"result": result.result,
				"cc_score": result.cc_score,
				"termination": result.termination,
				"plies": result.plies,
				"duration_seconds": f"{result.duration:.3f}",
				"opening_6plies": result.opening,
				"moves": " ".join(result.moves),
			}
		)


def summarize(rows: list[dict[str, str]]) -> dict[str, object]:
	wins = losses = draws = 0
	openings: Counter[str] = Counter()
	for row in rows:
		score = float(row["cc_score"])
		if score == 1.0:
			wins += 1
		elif score == 0.0:
			losses += 1
		else:
			draws += 1
		openings[row["opening_6plies"]] += 1
	n = wins + losses + draws
	score = (wins + 0.5 * draws) / n if n else 0.0
	return {
		"games": n,
		"wins": wins,
		"losses": losses,
		"draws": draws,
		"score": score,
		"unique_openings": len(openings),
		"most_common_openings": openings.most_common(10),
	}


def run_batch(
	emulator: SargonEmulator,
	engine: UCIEngine,
	output: Path,
	name: str,
	games: int,
	sargon_level: int,
	cc_force_color: Optional[chess.Color] = None,
) -> dict[str, object]:
	csv_path = output / f"{name}.csv"
	pgn_path = output / f"{name}.pgn"
	failure_path = output / f"{name}-failures.log"
	rows = load_completed(csv_path)
	start_index = len(rows)
	if start_index:
		log(f"resuming {name} at game {start_index + 1}/{games}")

	for index in range(start_index, games):
		for attempt in range(1, 4):
			try:
				result = play_game(
					emulator, engine, name, index, sargon_level, attempt,
					cc_force_color,
				)
				break
			except Exception as exc:
				with failure_path.open("a", encoding="utf-8") as handle:
					handle.write(
						f"{time.strftime('%Y-%m-%d %H:%M:%S')} game={index + 1} "
						f"attempt={attempt} error={exc!r}\n"
					)
				log(f"{name} game {index + 1} attempt {attempt} failed: {exc}")
				if attempt == 3:
					raise
				time.sleep(0.5)
		append_csv(csv_path, result)
		append_pgn(pgn_path, result, name, sargon_level, engine.skill)
		rows = load_completed(csv_path)
		summary = summarize(rows)
		log(
			f"{name} {index + 1}/{games}: cc65 {result.cc_color}, "
			f"{result.result} ({result.termination}), {result.plies} plies, "
			f"running W-L-D {summary['wins']}-{summary['losses']}-{summary['draws']}, "
			f"score {summary['score']:.3f}, openings {summary['unique_openings']}"
		)
		(output / f"{name}-summary.json").write_text(
			json.dumps(summary, indent=2) + "\n", encoding="utf-8"
		)
	return summarize(load_completed(csv_path))


def main() -> int:
	global MOVE_TIMEOUT

	parser = argparse.ArgumentParser()
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--port", type=int, default=DEFAULT_PORT)
	parser.add_argument(
		"--move-timeout", type=float, default=MOVE_TIMEOUT,
		help="seconds to allow one Sargon move (raise it for the deep levels)",
	)
	parser.add_argument("--smoke-games", type=int, default=0)
	parser.add_argument("--calibration-games", type=int, default=64)
	parser.add_argument("--match-games", type=int, default=512)
	parser.add_argument("--only", choices=("smoke", "calibration", "match", "auto"), default="auto")
	parser.add_argument("--sargon-level", type=int, choices=range(0, 7))
	parser.add_argument("--cc-skill", type=int, choices=CC65_SKILLS, default=1)
	# A colour split is the one thing this rig measures with half its games, and
	# the black side is where the open question is: 32 alternating games gave
	# five distinct black openings and one of them was played fourteen times.
	# Pinning the colour spends the whole run on the side being asked about
	parser.add_argument(
		"--cc-color", choices=("white", "black"), default=None,
		help="play every game with cc65 on this side (default: alternate)",
	)
	parser.add_argument(
		"--no-own-book", action="store_true",
		help="turn the engine's opening tables off (the pre-2026 behaviour)",
	)
	args = parser.parse_args()

	args.output.mkdir(parents=True, exist_ok=True)
	prepare_disk()
	if not A2M_BIN.is_file() or not UCI_BIN.is_file():
		raise BridgeError("a2m-v2 or cc65 UCI binary is missing")

	emulator = SargonEmulator(args.port)
	MOVE_TIMEOUT = args.move_timeout

	force_color = (
		None if args.cc_color is None
		else (chess.WHITE if args.cc_color == "white" else chess.BLACK)
	)
	engine = UCIEngine(skill=args.cc_skill, own_book=not args.no_own_book)

	def stop_handler(signum, frame):
		raise KeyboardInterrupt

	signal.signal(signal.SIGTERM, stop_handler)
	signal.signal(signal.SIGINT, stop_handler)

	try:
		emulator.start()
		if args.only == "smoke":
			games = args.smoke_games or 4
			level = args.sargon_level if args.sargon_level is not None else 1
			summary = run_batch(
				emulator, engine, args.output, "smoke", games, level,
				cc_force_color=force_color,
			)
			log(f"smoke complete: {json.dumps(summary, sort_keys=True)}")
			return 0

		if args.only in ("calibration", "auto"):
			calibration_name = (
				"calibration-l1"
				if args.cc_skill == 1
				else f"calibration-cc{args.cc_skill}-sargon1"
			)
			cal = run_batch(
				emulator,
				engine,
				args.output,
				calibration_name,
				args.calibration_games,
				1,
				cc_force_color=force_color,
			)
			level = 1
			recommended_cc_skill = args.cc_skill
			if float(cal["score"]) < 0.35 and args.cc_skill < 4:
				recommended_cc_skill += 1
			decision = {
				"calibration": cal,
				"competitive_score_floor": 0.35,
				"calibrated_cc_skill": args.cc_skill,
				"recommended_cc_skill": recommended_cc_skill,
				"selected_sargon_level": level,
			}
			(args.output / "level-decision.json").write_text(
				json.dumps(decision, indent=2) + "\n", encoding="utf-8"
			)
			log(
				f"calibration recommends cc65 Skill {recommended_cc_skill} "
				f"against Sargon Level 1 (score {float(cal['score']):.3f})"
			)
			if args.only == "calibration":
				return 0
		else:
			if args.sargon_level is None:
				decision_path = args.output / "level-decision.json"
				if not decision_path.is_file():
					raise BridgeError("--only match needs --sargon-level or level-decision.json")
				level = int(json.loads(decision_path.read_text())["selected_sargon_level"])
			else:
				level = args.sargon_level

		if args.only in ("match", "auto"):
			final = run_batch(
				emulator,
				engine,
				args.output,
				f"match-cc{args.cc_skill}-sargon{level}",
				args.match_games,
				level,
				cc_force_color=force_color,
			)
			log(f"measured match complete: {json.dumps(final, sort_keys=True)}")
		return 0
	except KeyboardInterrupt:
		log("interrupted; completed games are saved and the run can be resumed")
		return 130
	finally:
		engine.close()
		emulator.close()


if __name__ == "__main__":
	raise SystemExit(main())
