0.   Updates

* May 2020 - Advanced Build instructions at XI.
* May 2020 - I created a version for the Commander X16 (R37).
* May 2020 - I created a graphics version for the C64.
* May 2020 - [raxiss] created a nice Oric-1/Atmos/Telestrat port.
* Jan 2020 - Oliver Schmidt created a beautiful Apple II port. See IX below.
* There are images for all platforms in the releases tab.

I.   Introduction (Feb 14, 2014)

I started playing chess about 3 months ago and this got me wondering how
difficult it would be to make a computer chess game.  I decided to try and
since it's just a "for the fun of it" project, I decided to make it for the
Commodore 64, still my all-time favorite computer.  Using the excellent cc65
tools I could do it all in C and thus make it portable to other systems also.

I learnt that making the game isn't hard, but getting the computer to play
something that resembles a reasonable game is hard.  I don't know enough about
chess to get it right, but in this version of today, 14 Feb 2014, the AI is
not very good.

The game was developed on OS X using cc65 and the VICE emulator.

There is a video of the game here: http://youtu.be/bkA4vtwxaJg


II.  Use and keys

The colors here refer to the C64 version.  The terminal version has a minimal
working display but does try to somewhat match the colors of the C64.

The user controls an on-screen cursor.  The cursor changes color to indicate
a state.  The colors for selection are:
  Green - the piece can be selected
  Red - The piece cannot be selected as it doesn't have valid moves
  Purple - Empty tile or piece on the other side
  Blue - The currently selected piece
  Cyan - A valid destination for the currently selected piece

To move the cursor, use the cursor keys.  To select a piece, press the RETURN
key while the piece is selected.  To deselect the piece, press RETURN on the
same piece again, or press RUN/STOP.

To bring up the menu, press the M key, or the RUN/STOP key when no piece is
selected.  Pressing RUN/STOP in a menu backs out of the menu, to the previous
menu or back to the game.  Press RETURN to select a menu item and use the up
and down cursor keys to change the selection.

While a side is under human control, there are a few more options.  Press B to
toggle on/off a state showing on every tile how many of both black and white's
pieces can attack that tile.  Pressing A will toggle a highlight of all of the
pieces on the opposing side that attack the selected tile.  Pressing D will
toggle a highlight of all the pieces on the side currently playing's side that
can defend the selected tile.  All three of these options basically give a
visual representation of the Attack DB.  The colors are: For attackers Cyan
and for defenders Red.

Lastly, the game has an Undo/Redo stack that tracks the last 254 moves. Pressing
U will undo the last move and R will redo the last move.  In an AI/Human game,
the undo will undo the last AI and human player move, so the human player can
make a different move.


III.  Distribution

This version has code for a C64, using multi-colored text mode, and also code
for a terminal version using the curses library.  The terminal version was
only tested on OS X but I suspect it will run under Linux and Windows.


IV. Building from source

a) For the C64 (and other cc65 supported platforms):
Using a properly installed cc65 distribution, the C64 version should build
using make in the folder with the Makefile.  I suggest compiling for speed
but optimizing for size does save a bit of memory (1K at present):
make OPTIONS=optspeed

b) For a terminal version:
I built it on OS X using the following command line from the src folder:
cc -I. -lcurses -funsigned-char globals.c undo.c board.c cpu.c human.c \
frontend.c main.c term/platTerm.c -o chess


V.  Porting

The code in the src folder should compile on any system (cc65 has type char as
unsigned by default - the char type is almost the only type really used in the
code).  A new system will need platform specific implementations of the
functions in plat.h.  When I created the terminal port, it literally worked in
under an hour as it was mostly replacing cursor positioning and printing
function names, along with initialization and color management.  I had to redo
the log update and timer completely but still, the only file that needed
changes was the platTerm.c copy I made from plat64.c.

In the C64 specific folder under src is a data.c file which contains the
graphics characters to draw the pieces.  The layout and chosen bit-pattern of
that data is also explained in that file.


VI.    The AI & other thoughts on the code

The game is a fine 2-player chess game, but the computer is not a great chess
player.  My approach for the AI is this:

For each piece, calculate a score (see later) for the tile the piece currently
occupies.  Then look at all available moves for the piece, and score each
destination tile separately.  If the computer is fast, I would now "effect"
each move and run the same algorithm on the opponent, getting a "retaliation
score".  Effect the opponent move and run the algorithm again, getting a
subsequent score.   The accumulated "score - retaliation score + this side
next score - other side next retaliation score" sum, up to as many levels
deep as desired, would be the final score for that piece and destination.

Since the C64 isn't fast enough for all that, I have it calculate the score
for the piece where it stands and for all destinations.  The highest scoring
move, if valid, becomes the move for the piece.  The scores for all pieces
are then stack-ranked.  Some number of these are then chosen to pursue. I set
it to 16 (gWidth), making it pursues all pieces.

Pursuit of the best moves means doing the depth search for opponent moves and
back to own moves.  This is set to go to a level controlled by a variable
named gMaxLevel.

There is another variable, gDeepThoughts, that affects difficulty and speed.
This variable, when set to 1, ensure the moves chosen when evaluating best
moves, are valid.  It also, when set, causes the AttackDB to be updated.
Both of these are slow operations.  Not doing the work makes things a lot
faster, but obviously less accurate.  Especially the further away the
thinking gets from the current, accurate, state.

All three these variables are set from the difficulty selection if there's
an AI player.

Scoring a piece means this:  Positive points encourage the piece to move,
negative points discourage making a move.
A) For where the piece stands the score is calculated by looking at:
  If this piece is under attack increase chance to move, else decrease
  If this piece is being defended, decrease chance to move, else increase
  Providing support to a piece on own team, decrease
    if supported piece is under attack
      if only defender, decrease else increase
      if supported piece is more valuable, decrease
    not supporting a piece, increase

B) For every destination the piece can move to, score like this:
  If this piece will be under attack there, decrease
  If it will be defended there, increase else decrease
  If a piece is taken at dest, increase
  If providing support to a piece on same side from there, increase
    if that piece is under attack, increase
    if this will be the only defender of that piece, increase
    if the supported piece is more valuable than this piece, increase
  If attacking a piece on the other side from there, increase
    if the attacked piece is more valuable, increase
    if the attacked piece has no defenders, increase

The values for increase and decrease aren't always 1.  Some situations I
deemed more important so the value may be 2, or the value of the piece itself
for which I use: 1 PAWN, 3 KNIGHT, 3 BISHOP, 5 ROOK, 9 QUEEN, 10 KING, but
modified to 2+(3*value).  The +2 compensates for the +/-1's that encourage or
discourage a move, and the 3*value makes the value really meaningful.

There is another scoring opportunity that happens before any other.  It is
meant to take a holistic view of the board.  Currently, all it does is see
if the king has no moves then all of its neighboring pieces on the same side
are encouraged to move and; it encourages pawns to move so they can get to
promote.  The closer they get to the opposite side, the stronger the
encouragement.

I have no real plans to keep working on this project.  As stated, I wanted
to see how hard it would be, and now I know. I rushed this V1.0 release so,
sadly, I am sure there will be bugs.  There's also lots of room to experiment
with scores, values and relative importance of things like being under attack
vs. supporting another piece.

The code is reasonably clean but I really didn't design this as a game.  It
all evolved from the writing of the functions to build an array of valid moves
into a game.  The en passant and castling is somewhat hacked in, for example
and may be hard to make sense of.


VII.   Credits

The Makefile has the following notice:
###############################################################################
### Generic Makefile for cc65 projects - full version with abstract options ###
### V1.3.0(w) 2010 - 2013 Oliver Schmidt & Patryk "Silver Dream !" Łogiewa  ###
###############################################################################

cl65 --version prints the following in my installation:
cl65 V2.13.9 - (C) Copyright 1998-2011 Ullrich von Bassewitz

VIII.  Contact

Feel free to send me an email if you have any comments.  If you do make a port
or something else, I would love to hear about it!

swessels@email.com
Feb 14, 2014
Thank you!


IX.    Apple II Specific Version Information

General display uses the Apple II hires mode accessed via custom asm
functions.

Menu display uses the 4 line bottom text option of the Apple II hires mode via
cc65 CONIO functions.

All hires access is byte aligned, therefore the horizontal resolution is 40
(bytes).

Hires access is done via simple (binary) ROPs (raster operations) by using
actual 6502 (immediate) opcodes.

The C64 and Curses implementation both make heavy use of colors while Apple II
implementation mustn't depend on (but may benefit from) colors. Therefore the
user-operated cursor inverts the border of the current field. It's hard to find
a compromise between making the cursor visible well and showing the piece
"under" the cursor well. Additionally it is desirable to show different cursor
states (empty, invalid, valid). The approach chosen is to have different
thicknesses of the inverted border:

Valid: Thin
Invalid: Medium
Empty: Thick

When it comes to showing attackers/defenders (via the keys A / D) there's no
alternative to resorting to colors:

Attackers: Red
Defenders: Green

So the only field display variant left is the piece selected for moving. Instead
of introducing a third type of highlighting (beside border inversion and
coloring) it is simply colored Magenta. The reasoning:

On a monochrome display the user won't have much fun showing attackers/defenders
anyway. And without showing those the selected piece is the only colored (aka
striped) piece making it clearly visible.

On a color display a third color (beside attackers/defenders) works just fine.

As the Apple II doesn't have cursor-up and cursor-down keys the keys O and L
work as alternatives to the those cursor keys.

There's a video showing the Apple II version here: https://youtu.be/PPy-cg4ghDY

Oliver Schmidt
Jan 19, 2020


XI.    Build Instructions

All of the 8-Bit versions of cc65 Chess can be built using make.  

I recommend the game be built for speed, which also results in smaller file and
is essential for all targets.  This is done by using the OPTIONS=optspeed
command line to make.  See examples below.

When you type make (using GNU Make) the default behaviour is to make all of the
versions. Currently, that means the following (cc65 target name in brackets):

* Commodore 64 HiRes (c64)
* Commodore 64 Multicolor Text (c64.chr)
* Apple 2 (apple2)
* Oric-1/Atmos/Telestrat (atmos)
* Commander X16 (cx16)

Most platforms have an additional step that can be performed, which is to make a
program (prg), disk (dsk) or tape (tap) file.  Do make again, but with dsk
(Apple 2 dsk), tap (Oric tape), prg (C64 prg), cprg (c64.chr prg) or cxprg (cX16
prg) on the command line.

The two steps can be combined into a single make command, by using "all" as the
first target, i.e: 
make OPTIONS=optspeed all dsk tap prg cprg cxprg

Makeing a terminal version (using curses) - See IV (b) above.

Examples:
1) Make everything, and then make the dsk and tap files for the Apple and Oric.

make OPTIONS=optspeed

This will make the following files:
cc65-Chess.apple2
cc65-Chess.atmos
cc65-Chess.c64
cc65-Chess.c64.chr
cc65-Chess.cx16

make dsk tap prg cprg cxprg

This will make the following files:
cc65-Chess.tap
cc65-Chess.dsk
cc65-Chess-c64.prg
cc65-Chess-chr.prg
cc65-Chess-cx16.prg

Once you have used the OPTIONS=optspeed on the command-line, you do not have to
use it again since the options are saved in a file called Makefile.options.

2) Build just one version (let's say the Oric)

make OPTIONS=optspeed TARGETS=atmos tap
This will create a ready to run TAP file named cc65-Chess.tap

3) You can also start an emulator directly from make with the test command-line.

make OPTIONS=optspeed atmos test

This last command example is a good way of callimg make to build and test any of
the targets by itself, provided you have configured an emulator in the Makefile.
In this case, it will call the emulator with cc65-Chess.atmos but Oricutron
doesn't mind that the exytension isn't .tap.

Look for _EMUCMD in the Makefile.  You may have to specify a full path to the
emulator, and in some cases you may need to change the test: command itself. For
example, to run AppleWin I removed the $< from $(EMUCMD) $< in the test:
section, because AppleWin did not like the extra (cc64-Chess.apple2) file being
passed, and I had to give it the full path to cc64-Chess.dsk as part of apple2_EMUCMD.

Lastly - the CX16 and C64 versions use the same piece defenitions that Oliver
Schmidt added for the Apple II, and kindly agreed to let me use for these
versions as well.  See genPieces.cpp in the specific src folder for more details.
