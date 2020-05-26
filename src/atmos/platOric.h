/*
 *  platOric.h
 *  cc65 Chess
 *
 *  Implementation for Oric by [raxiss], May 2020
 *
 */

#ifndef __PLATORIC_H__
#define __PLATORIC_H__

/*-----------------------------------------------------------------------*/

#define KEY_LEFT        8
#define KEY_RIGHT       9
#define KEY_DOWN        10
#define KEY_UP          11

#define KEY_RETURN     13
#define KEY_ESC        27

/*-----------------------------------------------------------------------*/
void setup(void);
void hires(void);
int getkey(void);
void quit(void);

void hires_Draw(char xpos, char ypos, char xsize, char ysize, char rop, char *src);
void hires_Mask(char xpos, char ypos, char xsize, char ysize, char rop);
void plat_DrawChar(char xpos, char ypos, char rop, char c);
void plat_DrawString(char x, char y, char rop, char *s);


#endif /* __PLATORIC_H__ */
