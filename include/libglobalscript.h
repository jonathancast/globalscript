#ifndef _GLOBALSCRIPT_H_
#define _GLOBALSCRIPT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif                                                                

/* ========== Basic Thread Management Stuff ========== */

void gsfatal(char *err, ...);
void gswarning(char *err, ...);

/* ========== Global Script Program Calculus ========== */

typedef uintptr gsvalue;

typedef enum {
    gstyprim = 0,
    gstythunk = 1,
    gstystack = 2,
    gstyeooheap = 64,
    gstyeoostack = 65,
    gstyenosys = 66,
} gstypecode;

gstypecode gseval(gsvalue);

/* ========== API ========== */

typedef struct apiinstr
{
    gsvalue instr;
    gsvalue *dest;
} apiinstr;

typedef struct apithread_info {
    apiinstr *codebeg, *pc, *codeend;
} *apithread;

typedef struct {
    QLock *lock;
    apithread curthread;
} apithreadqueue;

typedef void apithreadmain(apithreadqueue *q);

extern apithread apithreadcreate(gsvalue prog, apithreadmain *threadmain, gsvalue *pres, int bindtoproc);

extern void apibindtothread(apithread t);

#if defined(__cplusplus)
}
#endif
#endif	/* _GLOBALSCRIPT_H_ */
