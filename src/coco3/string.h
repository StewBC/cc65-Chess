/*
 *	string.h
 *	cc65 Chess — Color Computer 3
 *
 *	engine.c includes <string.h> for memset.  CMOC has no string.h, and
 *	macOS cpp would otherwise pull the host libc and die.  cmoc.h is the
 *	CMOC libc; this header just re-exports it under the name the engine uses.
 */

#ifndef _STRING_H_
#define _STRING_H_

#include <cmoc.h>

#endif
