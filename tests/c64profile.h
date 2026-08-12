/*
 *	c64profile.h
 *	cc65 Chess - C64 profile instrumentation shared with the engine
 *
 *	This header is visible only when the test-only SEARCH_PROFILE build is
 *	requested.  Nothing declared here reaches a shipping target.
 */

#ifndef _C64PROFILE_H_
#define _C64PROFILE_H_

#define PROFILE_NONE		0
#define PROFILE_GEN_MOVES	1
#define PROFILE_GEN_CAPTURES	2
#define PROFILE_SCORE		3
#define PROFILE_SELECT		4
#define PROFILE_LEGALITY	5
#define PROFILE_BOARD		6
#define PROFILE_EVAL_MOVE	7
#define PROFILE_EVAL_END	8
#define PROFILE_EVAL_PHASE	9
#define PROFILE_HASH_DELTA	10
#define PROFILE_HISTORY		11
#define PROFILE_REPETITION	12
#define PROFILE_COMPONENTS	12

extern char geSearchProfile;

// A board-only make/unmake pair.  Evaluation, hash and history are suppressed
// so their separately doubled rows do not overlap this one.
void eng_ProfileBoardPair(const t_engMove *move);

#endif //_C64PROFILE_H_
