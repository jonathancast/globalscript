#ifndef _GLOBALSCRIPT_H_
#define _GLOBALSCRIPT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif   

/* ========== Basic Thread Management Stuff ========== */

void gsmain(int argc, char **argv);

void gsfatal(char *err, ...);
void gswarning(char *err, ...);

void gsassert(char *srcfile, int srcline, int passed, char *err, ...);
void gsdeny(char *srcfile, int srcline, int passed, char *err, ...);
void gsassert_ulong_eq(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);
void gsassert_ulong_ge(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);
void gsassert_ulong_le(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...);

/* ========== Global Script Program Calculus ========== */

typedef uintptr gsvalue;
typedef uintptr gscode;

typedef enum {
    gstyprim = 0,
    gstythunk = 1,
    gstystack = 2,
    gstywhnf = 3,
    gstyindir = 4,
    gstyexternal = 5,
    gstyunboxed = 6,
    gstyeooheap = 64,
    gstyeoostack = 65,
    gstyenosys = 66,
} gstypecode;

/* Define this yourself; this is your program's entry point */
extern void gsrun(gsvalue);

#define GS_MAX_PTR 0x80000000UL
    /* NOTE: 32-bit specific ^^^ */

gsvalue gsmakethunk(gscode, ...);

/* gstypecode gseval(gsvalue); */

gstypecode gsnoeval(gsvalue);
gstypecode gsevalunboxed(gsvalue);

#define IS_PTR(v) ((gsvalue)(v) < GS_MAX_PTR)

/* ========== Simple Segment Manager ========== */

typedef struct gs_block_class {
    gstypecode (*evaluator)(gsvalue);
        /* §ccode{gstypecode} mayn't return §ccode{gstythunk} */
    char *description;
} *registered_block_class;

struct gs_blockdesc {
    registered_block_class class;
};

#define BLOCK_SIZE (sizeof(gsvalue) * 0x40000)
#define BLOCK_CONTAINING(p) ((void*)((uintptr)(p) & ~(BLOCK_SIZE - 1)))
#define START_OF_BLOCK(p) ((void*)((uchar*)(p) + sizeof(*p)))
#define END_OF_BLOCK(p) ((void*)((uchar*)(p) + BLOCK_SIZE))

#define GS_EVALUATOR(p) (IS_PTR(p) ? ((struct gs_block_class*)BLOCK_CONTAINING(p))->evaluator : gsevalunboxed)

void *gs_sys_seg_alloc(registered_block_class cl);
void gs_sys_seg_free(void *);

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
    /* QLock *lock; */
    apithread curthread;
} apithreadqueue;

typedef void apithreadmain(apithreadqueue *q);

extern apithread apithreadcreate(gsvalue prog, apithreadmain *threadmain, gsvalue *pres, int bindtoproc);

extern void apibindtothread(apithread t);

#if defined(__cplusplus)
}
#endif
#endif	/* _GLOBALSCRIPT_H_ */
