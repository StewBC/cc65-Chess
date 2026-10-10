#ifndef ARCHIE_TEST_KERNEL_H
#define ARCHIE_TEST_KERNEL_H
typedef struct { int r[10]; } _kernel_swi_regs;
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
_kernel_oserror *_kernel_swi(int, _kernel_swi_regs *, _kernel_swi_regs *);
int _kernel_osbyte(int, int, int);
int _kernel_oswrch(int);
#endif
