/*
 *	frontend.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _FRONTEND_H_
#define _FRONTEND_H_

char frontend_Menu(char activeGame);
char frontend_GetPromotion(void);
void frontend_FormatLogString(void);
void frontend_LogMove(char atTop);

#endif //_FRONTEND_H_
