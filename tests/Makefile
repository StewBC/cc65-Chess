###############################################################################
### cc65 Chess - native test build                                          ###
###                                                                         ###
### Nothing here is ever compiled into a cc65 target.  The engine sources    ###
### are pulled in from ../src and linked against a stub platform layer, so   ###
### the game logic can be driven and checked on the host.                    ###
###                                                                         ###
###   make          build ./chesstest                                        ###
###   make test     build and run the pass/fail suite                        ###
###   make clean                                                             ###
###############################################################################

SRCDIR := ../src

# -funsigned-char matches cc65, where char is unsigned by default.  A lot of
# this code leans on that, so building without it will produce nonsense
# ENGINE_FAST_LEGAL=1 so the suite exercises the C2 candidate; shipping defaults off.
CFLAGS := -I$(SRCDIR) -funsigned-char -O2 -g -Wall -DEVAL_TUNING -DENGINE_FAST_LEGAL=1 -Wno-char-subscripts

# main.c is deliberately absent - the tests supply their own
ENGINE := \
	$(SRCDIR)/globals.c \
	$(SRCDIR)/engine.c \
	$(SRCDIR)/eval.c \
	$(SRCDIR)/search.c \
	$(SRCDIR)/board.c \
	$(SRCDIR)/cpu.c \
	$(SRCDIR)/human.c \
	$(SRCDIR)/undo.c \
	$(SRCDIR)/frontend.c

HARNESS := \
	main.c \
	testutil.c \
	platStub.c \
	engineperft.c \
	quiescence.c \
	budget.c \
	search.c \
	match.c \
	gamefuzz.c \
	castle.c \
	legality.c \
	repetition.c \
	opening.c \
	selfplay.c

# The engine headers are prerequisites too.  Without them an edit to search.h
# leaves a stale binary and the suite reports green for code that is no longer
# there - which cost an afternoon once and is invisible when it happens
HEADERS := $(wildcard $(SRCDIR)/*.h) testutil.h

chesstest: $(ENGINE) $(HARNESS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) $(HARNESS)

test: chesstest
	./chesstest all

# The UCI adapter, for playing other engines under a match runner.  Built
# WITHOUT -DEVAL_TUNING on purpose: that switch exists for the tuning harness
# and makes every node dearer, and the thing being measured has to be the thing
# that ships.  engineperft.c comes along only for its FEN parser
UCIFLAGS := -I$(SRCDIR) -funsigned-char -O2 -Wall -Wno-char-subscripts

uci: $(ENGINE) uci.c testutil.c engineperft.c platStub.c $(HEADERS)
	$(CC) $(UCIFLAGS) -o $@ $(ENGINE) uci.c testutil.c engineperft.c platStub.c

# The same adapter WITH the tuning switches, for A/B matches against an outside
# opponent - the ladder can then be re-run with one term off.  It is not the
# binary any published figure is measured with: tuning costs nodes, so the two
# builds must be shown to play the same games before an A/B means anything
uci-tuning: $(ENGINE) uci.c testutil.c engineperft.c platStub.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) uci.c testutil.c engineperft.c platStub.c

genbook: $(ENGINE) genbook.c testutil.c platStub.c $(HEADERS)
	$(CC) $(UCIFLAGS) -o $@ $(ENGINE) genbook.c testutil.c platStub.c

# book.epd - 256 openings, four plies deep - is checked in rather than built,
# and is deliberately NOT a dependency of anything.  It is the set every
# published figure in doc/strength.md was measured against, so it has to stay
# put: the same seeds give the same file only while genbook is unchanged, and
# a silently regenerated book would make a new result quietly incomparable with
# an old one.  Rebuild it only when you mean to, and expect to re-measure.
book: genbook
	./genbook 256 4 > book.epd

# the whole external-match setup, ready to run.  Needs a match runner and a
# reference engine - see gauntlet.py for what they are and why
gauntlet: uci genbook

# note book.epd is not removed here: make clean must not delete a tracked file
clean:
	rm -f chesstest uci uci-tuning genbook
	rm -rf chesstest.dSYM uci.dSYM uci-tuning.dSYM genbook.dSYM

.PHONY: test clean gauntlet book
