#include <conio.h>
int main(void)
{
    *(unsigned char*)0xFF06 |= 0x20;         /* TED bitmap mode */
    for(;;) cgetc();                         /* blocking */
}
