#ifndef _GLOBALSCRIPT_H_
#define _GLOBALSCRIPT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif   

/* §section A few pre-declarations since Global Script is idiotic */

struct gs_blockdesc;

/* §section Some C Macros */

#define MAX(a, b) ((a) < (b) ? (b) : (a))

/* §section{Basic Thread Management Stuff} */

void gsmain(int argc, char **argv);

int gsdebug;

void gsfatal(char *err, ...);
void gswarning(char *err, ...);

void gsfatal_unimpl(char *, int, char *, ...);

void gswerrstr_unimpl(char *, int, char *, ...);

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

void gsqueue_down(struct gsrpc_queue *);

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
    gssymcoerciondirective,
    gssymcoercionlable,
    gssymcoercionop,
    gssymseparator,
    gssymruneconstant,
    gssymstringconstant,
} gssymboltype;

typedef struct gsstring_value {
    ulong size;
    gssymboltype type;
    char name[];
} *gsinterned_string;

gsinterned_string gsintern_string(gssymboltype, char*);

struct gspos {
    gsinterned_string file;
    int lineno;
};
/* §todo{This is fine for string code files, but really should allow for core/Global Script source as well} */

#pragma varargck type "P" struct gspos
#pragma varargck type "y" gsinterned_string

/* §section Global Script Program Calculus */

/* §subsection Client-level Type-checking */

/* §subsubsection Constructors */

struct gstype;
enum gsprim_type_group;
struct gskind;
struct gsregistered_primset;
struct gstype_constr;
struct gstype_field;

struct gskind *gskind_compile_string(struct gspos, char *);

struct gstype *gstypes_compile_abstract(struct gspos, gsinterned_string, struct gskind *);
struct gstype *gstypes_compile_prim(struct gspos, enum gsprim_type_group, char *, char *, struct gskind *);
struct gstype *gstypes_compile_knprim(struct gspos, enum gsprim_type_group, struct gsregistered_primset *, gsinterned_string, struct gskind *);
struct gstype *gstypes_compile_unprim(struct gspos, enum gsprim_type_group, gsinterned_string, gsinterned_string, struct gskind *);
struct gstype *gstypes_compile_type_var(struct gspos, gsinterned_string, struct gskind *);
struct gstype *gstypes_compile_lambda(struct gspos, gsinterned_string, struct gskind *, struct gstype *);
struct gstype *gstypes_compile_forall(struct gspos, gsinterned_string, struct gskind *, struct gstype *);
struct gstype *gstypes_compile_lift(struct gspos, struct gstype *);
struct gstype *gstypes_compile_fun(struct gspos, struct gstype *, struct gstype *);
struct gstype *gstypes_compile_sum(struct gspos, int, ...);
struct gstype *gstypes_compile_sumv(struct gspos, int, struct gstype_constr *);
struct gstype *gstypes_compile_product(struct gspos, int, ...);
struct gstype *gstypes_compile_productv(struct gspos, int, struct gstype_field *);
struct gstype *gstypes_compile_ubproductv(struct gspos, int, struct gstype_field *);

struct gstype *gstype_supply(struct gspos, struct gstype *, struct gstype *);
struct gstype *gstype_apply(struct gspos, struct gstype *, struct gstype *);
struct gstype *gstype_instantiate(struct gspos, struct gstype *, struct gstype *);

struct gstype *gstypes_subst(struct gspos, struct gstype *, gsinterned_string, struct gstype *);

/* §subsubsection Views */

int gstype_expect_abstract(char *, char *, struct gstype *, char *);
int gstype_expect_prim(char *, char *, struct gstype *, enum gsprim_type_group, char *, char *);
int gstype_expect_var(char *, char *, struct gstype *, gsinterned_string);
int gstype_expect_forall(char *, char *, struct gstype *, gsinterned_string *, struct gstype **);
int gstype_expect_lift(char *, char *, struct gstype *, struct gstype **);
int gstype_expect_app(char *, char *, struct gstype *, struct gstype **, struct gstype **);
int gstype_expect_fun(char *, char *, struct gstype *, struct gstype **, struct gstype **);
int gstype_expect_lifted_fun(char *, char *, struct gstype *, struct gstype **, struct gstype **);
int gstype_expect_product(char *, char *, struct gstype *, int, ...);

/* §subsubsection Etc. */

struct gsfile_symtable;

struct gstype *gstype_get_definition(struct gspos, struct gsfile_symtable *, struct gstype *);

int gstypes_type_check(char *, char *, struct gspos, struct gstype *, struct gstype *);

/* §subsection Client-level Expression Manipulation */

struct gsfile_symtable;
typedef uintptr gsvalue;

gsvalue gscoerce(gsvalue, struct gstype *, struct gstype **, char *, char *, struct gsfile_symtable *, char *, ...);

gsvalue gsapply(struct gspos, gsvalue, gsvalue);

/* §subsection Runes */

char *gschartorune(char *, gsvalue *, char *, char *);
char *gsrunetochar(gsvalue, char *, char *, char *, char *);
gsvalue gscstringtogsstring(struct gspos, char *);

/* §subsection Lists */

gsvalue gsarraytolist(struct gspos, int, gsvalue *);

/* §subsection Records */

gsvalue gsemptyrecord(struct gspos);

/* §subsection Run-time Stuff */

typedef enum {
    gstythunk,
    gstystack,
    gstyblocked,
    gstywhnf,
    gstyindir,
    gstyerr,
    gstyimplerr,
    gstyunboxed,
    gstyenosys = 64,
    gstyeoothreads,
    gstyeoostack,
} gstypecode;

/* Define this yourself; this is your program's entry point */
extern void gsrun(char *, struct gsfile_symtable *, struct gspos, gsvalue, struct gstype *, int, char **);

#define GS_MAX_PTR 0x80000000UL
    /* NOTE: 32-bit specific ↑↑↑.  Thought: would §ccode{1UL << (sizeof(gsvalue) * 8 - 1)} work? */

void *gsreserveheap(ulong);

gstypecode gsnoeval(gsvalue);
gsvalue gsnoindir(gsvalue);

gstypecode gsevalunboxed(gsvalue);

gstypecode gswhnfeval(gsvalue);
gsvalue gswhnfindir(gsvalue);

#define IS_PTR(v) ((gsvalue)(v) < GS_MAX_PTR)

#define GS_SLOW_EVALUATE(v) (IS_PTR(v) ? (*GS_EVALUATOR(v))(v) : gstyunboxed)
#define GS_REMOVE_INDIRECTIONS(v) ((*GS_INDIRECTION_DEREFENCER(v))(v))

int gsiserror_block(struct gs_blockdesc *);
int gsisimplementation_failure_block(struct gs_blockdesc *);
int gsisheap_block(struct gs_blockdesc *);
int gsisrecord_block(struct gs_blockdesc *);
int gsisconstr_block(struct gs_blockdesc *);
int gsiseprim_block(struct gs_blockdesc *);

struct gsheap_item {
    Lock lock;
    struct gspos pos;
    enum {
        gsclosure,
        gsapplication,
        gseval,
        gsindirection,
    } type;
};

struct gsclosure {
    struct gsheap_item hp;
    struct gsbco *code;
    uint numfvs;
    gsvalue fvs[];
};

struct gsbco {
    enum {
        gsbc_expr,
        gsbc_forcecont,
        gsbc_eprog,
    } tag;
    struct gspos pos;
    ulong size;
    ulong numglobals, numsubexprs, numfvs, numargs;
};

struct gsapplication {
    struct gsheap_item hp;
    gsvalue fun;
    uint numargs;
    gsvalue arguments[];
};

struct gseval {
    struct gsheap_item hp;
    struct ace_thread *thread;
};

struct gsindirection {
    struct gsheap_item hp;
    gsvalue target;
};

struct gserror {
    struct gspos pos;
    enum {
        gserror_undefined,
        gserror_generated,
    } type;
    char message[];
};

char *gserror_format(char *, char *, struct gserror *);

struct gsimplementation_failure {
    struct gspos cpos, srcpos;
    char message[];
};

char *gsimplementation_failure_format(char *, char *, struct gsimplementation_failure *);

struct gsrecord {
    struct gspos pos;
    int numfields;
    gsvalue fields[];
};

struct gsconstr {
    struct gspos pos;
    int constrnum;
    int numargs;
    gsvalue arguments[];
};

struct gseprim {
    struct gspos pos;
    int index;
    gsvalue arguments[];
};

/* §section{Primitives} */

struct gsregistered_primset {
    char *name;
    struct gsregistered_primtype *types;
    struct gsregistered_prim *operations;
};

enum gsprim_type_group {
    gsprim_type_defined,
    gsprim_type_elim,
    gsprim_type_api,
};

struct gsregistered_primtype {
    char *name;
    char *file;
    int line;
    enum gsprim_type_group prim_type_group;
    char *kind;
};

struct gsregistered_prim {
    char *name;
    char *file;
    int line;
    enum {
        gsprim_operation_api,
    } group;
    char *apitype;
    char *type;
    int index;
};

/* Define this yourself; this should register your program's primsets to */
extern void gsadd_client_prim_sets(void);

void gsprims_register_prim_set(struct gsregistered_primset *);
struct gsregistered_primset *gsprims_lookup_prim_set(char *name);

struct gsregistered_primtype *gsprims_lookup_type(struct gsregistered_primset *, char*);

struct gsregistered_prim *gsprims_lookup_prim(struct gsregistered_primset *, char *);

/* §section{Simple Segment Manager} */

typedef struct gs_block_class {
    gstypecode (*evaluator)(gsvalue);
        /* §ccode{evaluator} mayn't return §ccode{gstythunk} */
    gsvalue (*indirection_dereferencer)(gsvalue);
    char *description;
} *registered_block_class;

struct gs_blockdesc {
    registered_block_class class;
};

#define BLOCK_SIZE (sizeof(gsvalue) * 0x40000)
#define BLOCK_CONTAINING(p) ((void*)((uintptr)(p) & ~(BLOCK_SIZE - 1)))
#define START_OF_BLOCK(p) ((void*)((uchar*)(p) + sizeof(*p)))
#define END_OF_BLOCK(p) ((void*)((uchar*)(p) + BLOCK_SIZE))

#define GS_EVALUATOR(p) (IS_PTR(p) ? ((struct gs_blockdesc *)BLOCK_CONTAINING(p))->class->evaluator : gsevalunboxed)
#define GS_INDIRECTION_DEREFENCER(p) (((struct gs_blockdesc *)BLOCK_CONTAINING(p))->class->indirection_dereferencer)

void *gs_sys_seg_alloc(registered_block_class cl);
void gs_sys_seg_free(void *);

void *gs_sys_seg_suballoc(registered_block_class, void**, ulong, ulong);

/* §section ACE */

void ace_up(void);
void ace_down(void);

/* §section API */

struct api_thread;

struct api_thread_table {
    enum api_prim_execution_state (*thread_term_status)(struct api_thread *);
};

void *api_thread_client_data(struct api_thread *);
void api_take_thread(struct api_thread *);
void api_release_thread(struct api_thread *);

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

enum api_prim_execution_state {
    api_st_success,
    api_st_error,
    api_st_blocked,
};

typedef enum api_prim_execution_state (api_prim_executor)(struct api_thread *, struct gseprim *, gsvalue *);

struct api_prim_table {
    int numprims;
    api_prim_executor *execs[];
};

api_prim_executor api_thread_handle_prim_unit;

typedef void (api_termination_callback)(void);
void api_at_termination(api_termination_callback *);

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void apisetupmainthread(struct api_process_rpc_table *, struct api_thread_table *, void *, struct api_prim_table *, gsvalue);

/* If (and only if) the current thread is hard, these will send a done message (§c{api_std_rpc_done}) to the corresponding process */
void api_done(struct api_thread *);

/* If (and only if) the current thread is hard, these will send an abend message (§c{api_std_rpc_abend}) to the corresponding process */
void api_abend(struct api_thread *, char *, ...);
void api_abend_unimpl(struct api_thread *, char *, int, char *, ...);

/* If (and only if) the destination thread is hard, these will send an abend message (§c{api_std_rpc_abend}) to the corresponding process */
void api_thread_post(struct api_thread *, char *, ...);
void api_thread_post_unimpl(struct api_thread *, char *, int, char *, ...);

rpc_handler api_main_process_handle_rpc_done;
rpc_handler api_main_process_handle_rpc_abend;

/* §section Buffered I/O library */

#ifndef NO_IO_ROUTINES
struct gsbio_dir {
    long size;
    Dir d;
};

struct gsbio_dir *gsbio_stat(char *filename);

struct uxio_ichannel *gsbio_device_iopen(char *filename, int omode);
struct uxio_dir_ichannel *gsbio_dir_iopen(char *filename, int omode);
struct uxio_ichannel *gsbio_envvar_iopen(char *name);
long gsbio_device_iclose(struct uxio_ichannel *);

long gsbio_device_getline(struct uxio_ichannel *chan, char *line, long max);
long gsbio_get_contents(struct uxio_ichannel *chan, char *buf, long max);

struct gsbio_dir *gsbio_read_stat(struct uxio_dir_ichannel *);

int gsbio_idevice_at_eof(struct uxio_ichannel *chan);
#endif

#if defined(__cplusplus)
}
#endif
#endif	/* _GLOBALSCRIPT_H_ */
