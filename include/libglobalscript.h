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
typedef uintptr gscode;

typedef enum {
    gstyprim = 0,
    gstythunk = 1,
    gstystack = 2,
    gstywhnf = 3,
    gstyeooheap = 64,
    gstyeoostack = 65,
    gstyenosys = 66,
} gstypecode;

#define GS_MAX_PTR 0x80000000
    /* NOTE: 32-bit specific ^^^ */

gsvalue gsmakethunk(gscode, ...);

/* gstypecode gseval(gsvalue); */

#define IS_PTR(v) ((gsvalue)(v) >= GS_MAX_PTR)

/* ========== Memory Allocation and Management ========== */

typedef gstypecode (*registered_block_type)(gsvalue, gsvalue*);

typedef struct blockheader {
    registered_block_type type;
} blockheader;
/* Note: blockheader should be one word exactly; is it? */

#define BLOCK_SIZE (sizeof(gsvalue) * 0x40000)
#define START_OF_BLOCK(p) ((void*)((uchar*)p + sizeof(*p)))
#define END_OF_BLOCK(p) ((void*)((uchar*)p + BLOCK_SIZE))

void *gs_sys_block_alloc(registered_block_type);
void gs_sys_block_free(void *);

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
