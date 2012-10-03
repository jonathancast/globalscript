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

/* §section Threading */

#ifdef __UNIX__
/* We always want to use the Plan 9 version of the locking routines;
   so on Unix, use a local copy of those routines (§emph{not} the P9P versions).
*/
#define lock rp9lock
#define unlock rp9unlock
#define canlock rp9canlock
#define Lock rp9Lock

typedef
struct Lock {
	int	val;
} Lock;

extern int	_tas(int*);

extern	void	lock(Lock*);
extern	void	unlock(Lock*);
extern	int	canlock(Lock*);
#endif

int gscreate_thread_pool(void (*)(void *), void *, ulong);

/* §section RPC */

struct gsrpc_queue;

struct gsrpc {
    Lock lock;
    int tag;
    enum {
        gsrpc_failed = -1,
        gsrpc_pending,
        gsrpc_running,
        gsrpc_succeeded,
    } status;
    char *err;
};

struct gsrpc_queue *gsqueue_alloc(void);

struct gsrpc *gsqueue_rpc_alloc(ulong);

struct gsrpc *gsqueue_get_rpc(struct gsrpc_queue *);
void gsqueue_send_rpc(struct gsrpc_queue *, struct gsrpc *);

typedef void (rpc_handler)(struct gsrpc *);

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
    gssymfieldlable,
} gssymboltype;

typedef struct gsstring_value {
    ulong size;
    gssymboltype type;
    char name[];
} *gsinterned_string;

/* §section Global Script Program Calculus */

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

struct gsheap_item {
    gsinterned_string file;
    int lineno;
    enum {
        gsclosure,
    } type;
};

struct gsclosure {
    struct gsheap_item hp;
    struct gsbco *code;
    gsvalue fvs[];
};

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

struct api_thread;

enum {
    api_std_rpc_done,
    api_std_rpc_abend,
    api_std_rpc_numrpcs,
};
struct api_process_rpc_table {
    char *name;
    int numrpcs;
    rpc_handler *handlers[];
};
rpc_handler api_main_process_unimpl_rpc;

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void apisetupmainthread(struct api_process_rpc_table *, gsvalue);

/* If (and only if) the current thread is hard, these will send an abend message (message 1) to the corresponding process */
void api_abend(struct api_thread *, char *, ...);
void api_abend_unimpl(struct api_thread *, char *, int, char *, ...);

rpc_handler api_main_process_handle_rpc_abend;

#if defined(__cplusplus)
}
#endif
#endif	/* _GLOBALSCRIPT_H_ */
