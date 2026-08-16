/*
 *	chess.r
 *	cc65 Chess — Macintosh 68k
 *
 *	creator Cchs.  SIZE comes from Retro68APPL.r (1 MB).
 *	ICN# / ics# live in icon.r (generated).
 */

#include "Types.r"

type 'Cchs' as 'STR ';

resource 'Cchs' (0, purgeable)
{
	"cc65 Chess by Stefan Wessels"
};

resource 'BNDL' (128, purgeable)
{
	'Cchs', 0,
	{
		'ICN#', { 0, 128 },
		'FREF', { 0, 128 }
	}
};

resource 'FREF' (128, purgeable)
{
	'APPL', 0, ""
};
