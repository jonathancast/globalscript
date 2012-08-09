#ifndef _GLOBALSCRIPT_H_
#define _GLOBALSCRIPT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif   

/* §section{A few pre-declarations since Global Script is idiotic} */

struct gs_blockdesc;

/* §section{Basic Thread Management Stuff} */

void gsmain(int argc, char **argv);

void gsfatal(char *err, ...);
void gswarning(char *err, ...);

void gsfatal_unimpl(char *, int, char *, ...);

void gsassert(char *srcfile, int srcline, int passed, char *err, ...);

/* §section Internal error-reporting stuff */

typedef enum {
    gssymfilename,
    gssymdatadirective,
    gssymdatalable,
    gssymcodedirective,
    gssymcodeop,
    gssymcodelable,
    gssymtypedirective,
    gssymtypeop,
    gssymtypelable,
    gssymkindexpr,
    gssymprimsetlable,
    gssymconstrlable,
} gssymboltype;

typedef struct gsstring_value {
    ulong size;
    gssymboltype type;
    char name[];
} *gsinterned_string;

/* §section{Global Script Program Calculus} */

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
    /* NOTE: 32-bit specific ↑↑↑.  Thought: would §ccode{1UL << (sizeof(gsvalue) * 8 - 1)} work? */

gsvalue gsmakethunk(gscode, ...);

void *gsreserveheap(ulong);

gstypecode gsnoeval(gsvalue);
gstypecode gsevalunboxed(gsvalue);
gstypecode gswhnfeval(gsvalue);

#define IS_PTR(v) ((gsvalue)(v) < GS_MAX_PTR)

#define GS_SLOW_EVALUATE(v) (IS_PTR(v) ? (*GS_EVALUATOR(v))(v) : gstyunboxed)

int gsiserror_block(struct gs_blockdesc *);

struct gserror {
    gsinterned_string file;
    int lineno;
    enum {
        gserror_undefined,
    } type;
};

/* §section{Primitives} */

struct gsregistered_primset {
    char *name;
    struct gsregistered_primtype *types;
};

enum gsprim_type_group {
    gsprim_type_defined,
};

struct gsregistered_primkind {
    enum {
        gsprim_kind_unlifted,
    } node;
};

struct gsregistered_primtype {
    char *name;
    char *file;
    int line;
    enum gsprim_type_group prim_type_group;
    struct gsregistered_primkind *kind;
};

void gsprims_register_prim_set(struct gsregistered_primset *);
struct gsregistered_primset *gsprims_lookup_prim_set(char *name);

struct gsregistered_primtype *gsprims_lookup_type(struct gsregistered_primset *, char*);

/* §section{Simple Segment Manager} */

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

#define GS_EVALUATOR(p) (IS_PTR(p) ? ((struct gs_blockdesc*)BLOCK_CONTAINING(p))->class->evaluator : gsevalunboxed)

void *gs_sys_seg_alloc(registered_block_class cl);
void gs_sys_seg_free(void *);

void *gs_sys_seg_suballoc(registered_block_class, void**, ulong, ulong);

/* §section{API} */

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
