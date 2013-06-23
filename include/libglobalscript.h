#ifndef _GLOBALSCRIPT_H_
#define _GLOBALSCRIPT_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif   

/* §section A few pre-declarations since C is idiotic */

struct gs_blockdesc;

/* §section Some C Macros */

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* §section Basic Thread Management Stuff */

void gsmain(int argc, char **argv);

int gsdebug;

void gsfatal(char *err, ...);
void gswarning(char *err, ...);

void gsfatal_unimpl(char *, int, char *, ...);

void gswerrstr_unimpl(char *, int, char *, ...);

void gsassert(char *srcfile, int srcline, int passed, char *err, ...);

#define UNIMPL(s) "%s:%d: " s " next", __FILE__, __LINE__
#define UNIMPL_NL(s) "%s:%d: " s " next\n", __FILE__, __LINE__

/* §section Command-line Arguments You Might Be Interested In */

extern int gsflag_stat_collection;

/* §section Statistics collection */

void gsstatprint(char *, ...);

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

extern	void	lock(Lock*);
extern	void	unlock(Lock*);
extern	int	canlock(Lock*);
#endif

int gscreate_thread_pool(void (*)(void *), void *, ulong);

/* §section RPC */

struct gsstringbuilder;

struct gsrpc_queue;

typedef struct gsrpc *gsrpc_gccopy(struct gsstringbuilder *, struct gsrpc *);
typedef int gsrpc_gcevacuate(struct gsstringbuilder *, struct gsrpc *);

struct gsrpc {
    Lock lock;
    int tag;
    enum {
        gsrpc_failed = -1,
        gsrpc_pending,
        gsrpc_running,
        gsrpc_succeeded,
    } status;
    struct gsrpc *forward;
    gsrpc_gccopy *gccopy;
    gsrpc_gcevacuate *gcevacuate;
    struct gsstringbuilder *err;
};

struct gsrpc_queue *gsqueue_alloc(void);

struct gsrpc *gsqueue_rpc_alloc(ulong, gsrpc_gccopy *, gsrpc_gcevacuate *);

struct gsrpc *gsqueue_try_get_rpc(struct gsrpc_queue *);
void gsqueue_send_rpc(struct gsrpc_queue *, struct gsrpc *);

void gsqueue_down(struct gsrpc_queue *);

typedef void (gsrpc_handler)(struct gsrpc *);

int gsqueue_gc_trace(struct gsstringbuilder *, struct gsrpc_queue **);

int gsqueue_rpc_gc_trace(struct gsstringbuilder *, struct gsrpc **);

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
    gssymregexconstant,
    gssymnaturalconstant,
} gssymboltype;

typedef struct gsstring_value {
    ulong size;
    ulong hash;
    gssymboltype type;
    char name[];
} *gsinterned_string;

gsinterned_string gsintern_string(gssymboltype, char*);

int gs_gc_trace_interned_string(struct gsstringbuilder *, gsinterned_string *);

struct gspos {
    gsinterned_string file;
    int lineno, columnno;
};

int gs_gc_trace_pos(struct gsstringbuilder *, struct gspos *);

#pragma varargck type "P" struct gspos
#pragma varargck type "y" gsinterned_string

/* §section Global Script Program Calculus */

#define MAX_NUM_REGISTERS 0x100

/* §subsection Client-level Type-checking */

/* §subsubsection General Interrogation of Symbol Tables */

struct gsfile_symtable;

struct gspos gstype_get_location(struct gspos, struct gsfile_symtable *, gsinterned_string);

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
struct gstype *gstypes_compile_exists(struct gspos, gsinterned_string, struct gskind *, struct gstype *);
struct gstype *gstypes_compile_lift(struct gspos, struct gstype *);
struct gstype *gstypes_compile_fun(struct gspos, struct gstype *, struct gstype *);
struct gstype *gstypes_compile_sum(struct gspos, int, ...);
struct gstype *gstypes_compile_sumv(struct gspos, int, struct gstype_constr *);
struct gstype *gstypes_compile_ubsumv(struct gspos, int, struct gstype_constr *);
struct gstype *gstypes_compile_product(struct gspos, int, ...);
struct gstype *gstypes_compile_productv(struct gspos, int, struct gstype_field *);
struct gstype *gstypes_compile_ubproduct(struct gspos, int, ...);
struct gstype *gstypes_compile_ubproductv(struct gspos, int, struct gstype_field *);

struct gstype *gstype_apply(struct gspos, struct gstype *, struct gstype *);
struct gstype *gstype_instantiate(struct gspos, struct gstype *, struct gstype *);

struct gstype *gstypes_subst(struct gspos, struct gstype *, gsinterned_string, struct gstype *);

/* §subsubsubsection Common stdlib types */

struct gstype *gstypes_compile_list(struct gspos, struct gstype *);
struct gstype *gstypes_compile_rune(struct gspos);

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

struct gstype *gstype_get_definition(struct gspos, struct gsfile_symtable *, struct gstype *);

int gstypes_type_check(struct gsstringbuilder *, struct gspos, struct gstype *, struct gstype *);

/* §subsection Client-level Expression Manipulation */

struct gsfile_symtable;
typedef uintptr gsvalue;

gsvalue gsapply(struct gspos, gsvalue, gsvalue);
gsvalue gsnapplyv(struct gspos, gsvalue, int, gsvalue *);

/* §subsection Constructors */

gsvalue gsconstr(struct gspos, int, int, ...);
gsvalue gsconstrv(struct gspos, int, int, gsvalue *);

/* §subsection Booleans */

gsvalue gstrue(struct gspos);
gsvalue gsfalse(struct gspos);

/* §subsection Runes */

char *gschartorune(char *, gsvalue *, char *, char *);
char *gsrunetochar(gsvalue, char *, char *);
gsvalue gscstringtogsstring(struct gspos, char *);

/* §subsection Natural Numbers */

char *gsnaturaltochar(char *, char *, gsvalue, char *, char *);

/* §subsection Lists */

gsvalue gsarraytolist(struct gspos, int, gsvalue *);

/* §subsection Records */

gsvalue gsemptyrecord(struct gspos);
gsvalue gsrecordv(struct gspos, int, gsvalue *);

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
} gstypecode;

/* Define these yourself; these are your program's entry points */
extern void gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *);
extern void gscheck_program(char *, struct gsfile_symtable *, struct gspos, struct gstype *);
extern int gs_client_pre_ace_gc_trace_roots(struct gsstringbuilder *);
extern void gsrun(char *, struct gspos, gsvalue, int, char **);

#define GS_MAX_PTR 0x80000000UL
    /* NOTE: 32-bit specific ↑↑↑.  Thought: would §ccode{1UL << (sizeof(gsvalue) * 8 - 1)} work? */

void *gsreserveheap(ulong);

gstypecode gsnoeval(struct gspos, gsvalue);
gsvalue gsnoindir(struct gspos, gsvalue);
gsvalue gsunimplgc(struct gsstringbuilder *, gsvalue);

gstypecode gsevalunboxed(struct gspos, gsvalue);

gstypecode gswhnfeval(struct gspos, gsvalue);
gsvalue gswhnfindir(struct gspos, gsvalue);

#define IS_PTR(v) ((gsvalue)(v) < GS_MAX_PTR)

#define GS_SLOW_EVALUATE(pos, v) (IS_PTR(v) ? (*GS_EVALUATOR(v))(pos, v) : gstyunboxed)
#define GS_REMOVE_INDIRECTION(pos, v) ((*GS_INDIRECTION_DEREFENCER(v))(pos, v))

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
        gsgcforward,
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
        gsbc_strictcont,
        gsbc_ubcasecont,
        gsbc_impprog,
        gsbc_gcforward,
    } tag;
    struct gspos pos;
    ulong size;
    ulong numglobals, numsubexprs, numfvs, numargs;
};

int gs_gc_trace_bco(struct gsstringbuilder *, struct gsbco **);

struct gsapplication {
    struct gsheap_item hp;
    gsvalue fun;
    uint numargs;
    gsvalue arguments[];
};

struct gseval {
    struct gsheap_item hp;
    struct gsbc_cont_update *update;
};

struct gsindirection {
    struct gsheap_item hp;
    gsvalue target;
};

struct gsgcforward {
    struct gsheap_item hp;
    gsvalue dest;
};

struct gserror {
    struct gspos pos;
    enum {
        gserror_undefined,
        gserror_generated,
        gserror_forward,
    } type;
};

struct gserror_message {
    struct gserror err;
    char message[];
};

struct gserror_forward {
    struct gserror err;
    gsvalue dest;
};

struct gserror *gserror(struct gspos, char *, ...);

char *gserror_format(char *, char *, struct gserror *);

struct gsimplementation_failure {
    struct gspos cpos, srcpos;
    struct gsimplementation_failure *forward;
    char message[];
};

struct gsimplementation_failure *gsunimpl(char *, int, struct gspos, char *, ...);

char *gsimplementation_failure_format(char *, char *, struct gsimplementation_failure *);

struct gsrecord {
    struct gspos pos;
    enum {
        gsrecord_fields,
        gsrecord_gcforward,
    } type;
};

struct gsrecord_fields {
    struct gsrecord rec;
    int numfields;
    gsvalue fields[];
};

struct gsconstr {
    struct gspos pos;
    enum {
        gsconstr_args,
        gsconstr_gcforward,
    } type;
};

struct gsconstr_args {
    struct gsconstr c;
    int constrnum;
    int numargs;
    gsvalue arguments[];
};

struct gseprim {
    struct gspos pos;
    enum {
        eprim_prim,
        eprim_forward,
    } type;
    union {
        struct {
            int index;
            int numargs;
            gsvalue arguments[];
        } p;
        struct {
            struct gseprim *dest;
        } f;
    };
};

/* §section Primitives */

struct ace_thread;

typedef int gsprim_handler(struct ace_thread *, struct gspos, int, gsvalue *, gsvalue *);
typedef int gsubprim_handler(struct ace_thread *, struct gspos, int, gsvalue *);
typedef int gslprim_handler(struct ace_thread *, struct gspos, int, gsvalue *);

struct gslprim_blocking;

typedef int gslprim_resumption_handler(struct ace_thread *, struct gspos, struct gslprim_blocking *);
typedef struct gslprim_blocking *gslprim_gccopy_handler(struct gsstringbuilder *, struct gslprim_blocking *);
typedef int gslprim_gcevacuate_handler(struct gsstringbuilder *, struct gslprim_blocking *);

struct gslprim_blocking {
    gslprim_resumption_handler *resume;
    gslprim_gccopy_handler *gccopy;
    gslprim_gcevacuate_handler *gcevacuate;
    struct gslprim_blocking *forward;
};

void *gslprim_blocking_alloc(long, gslprim_resumption_handler *, gslprim_gccopy_handler *, gslprim_gcevacuate_handler *);

int gslprim_blocking_trace(struct gsstringbuilder *, struct gslprim_blocking **);

/* §ags{.lprim} and §ags{.ubprim} handlers must finish by returning a call to one of these functions: */
int gsprim_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);
int gsprim_error(struct ace_thread *, struct gserror *);
int gsprim_return(struct ace_thread *, struct gspos, gsvalue);
int gsprim_return_ubsum(struct ace_thread *, struct gspos, int, int, ...);
int gsprim_block(struct ace_thread *, struct gspos, struct gslprim_blocking *);

struct gsregistered_primset {
    char *name;
    struct gsregistered_primtype *types;
    struct gsregistered_prim *operations;
    gsprim_handler **exec_table;
    gsubprim_handler **ubexec_table;
    gslprim_handler **lexec_table;
};

enum gsprim_type_group {
    gsprim_type_defined,
    gsprim_type_intr,
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

enum gsprim_group {
    gsprim_operation,
    gsprim_operation_unboxed,
    gsprim_operation_lifted,
    gsprim_operation_api,
};

struct gsregistered_prim {
    char *name;
    char *file;
    int line;
    enum gsprim_group group;
    char *apitype;
    char *type;
    int index;
};

/* Define this yourself; this should register your program's primsets to */
extern void gsadd_client_prim_sets(void);

void gsprims_register_prim_set(struct gsregistered_primset *);
struct gsregistered_primset *gsprims_lookup_prim_set(char *name);
struct gsregistered_primset *gsprims_lookup_prim_set_by_index(int);
int gsprims_prim_set_index(struct gsregistered_primset *);

struct gsregistered_primtype *gsprims_lookup_type(struct gsregistered_primset *, char*);

struct gsregistered_prim *gsprims_lookup_prim(struct gsregistered_primset *, char *);

/* §section Simple Segment Manager */

typedef struct gs_block_class {
    gstypecode (*evaluator)(struct gspos, gsvalue);
        /* §ccode{evaluator} mayn't return §ccode{gstythunk} */
    gsvalue (*indirection_dereferencer)(struct gspos, gsvalue);
    gsvalue (*gc_trace)(struct gsstringbuilder *, gsvalue);
    char *description;
} *registered_block_class;

struct gs_blockdesc {
    registered_block_class class;
};

#define BLOCK_SIZE (sizeof(gsvalue) * 0x40000)
#define BLOCK_CONTAINING(p) ((void*)((uintptr)(p) & ~(BLOCK_SIZE - 1)))
#define START_OF_BLOCK(p) ((void*)((uchar*)(p) + sizeof(*p)))
#define END_OF_BLOCK(p) ((void*)((uchar*)(p) + BLOCK_SIZE))

#define CLASS_OF_BLOCK_CONTAINING(p) (((struct gs_blockdesc *)BLOCK_CONTAINING(p))->class)
#define GS_EVALUATOR(p) (IS_PTR(p) ? CLASS_OF_BLOCK_CONTAINING(p)->evaluator : gsevalunboxed)
#define GS_INDIRECTION_DEREFENCER(p) (CLASS_OF_BLOCK_CONTAINING(p)->indirection_dereferencer)
#define GS_GC_TRACER(p) (CLASS_OF_BLOCK_CONTAINING(p)->gc_trace)

typedef uintptr gsumemorysize; /* TODO: Horrid hack */

void *gs_sys_block_alloc(registered_block_class cl);

/* ↓ Deprecated */
void *gs_sys_block_suballoc(registered_block_class, void**, ulong, ulong);

struct gs_sys_global_block_suballoc_info {
    struct gs_block_class descr;
    ulong align;
    Lock lock;
    void *nursury;
    struct gs_sys_global_block_suballoc_info *next;
};
void *gs_sys_global_block_suballoc(struct gs_sys_global_block_suballoc_info *, ulong sz);

struct gs_sys_aligned_block_suballoc_info {
    struct gs_block_class descr;
    ulong align;
    Lock lock;
    void *nursury;
    struct gs_sys_aligned_block_suballoc_info *next;
};
void gs_sys_aligned_block_suballoc(struct gs_sys_aligned_block_suballoc_info *, void **, void **);

gsumemorysize gs_sys_memory_allocated_size(void);
int gs_sys_memory_exhausted(void);

/* §paragraph{GC functions for main thread} */
int gs_sys_should_gc(void);
void gs_sys_wait_for_gc(void);
int gs_sys_start_gc(struct gsstringbuilder *);
#define GS_GC_TRACE(err, pv) (*(pv) && IS_PTR(*(pv)) && gs_sys_block_in_gc_from_space((void*)(*(pv))) ? ((gctemp = GS_GC_TRACER(*(pv))(err, *(pv))) ? (*(pv) = gctemp, 0) : -1) : 0)
int gs_sys_finish_gc(struct gsstringbuilder *);
void gs_sys_gc_failed(char *);

/* §paragraph{Long-form form GC functions for secondary thread} */
int gs_sys_gc_want_collection(void);
int gs_sys_wait_for_collection_to_finish(struct gsstringbuilder *);
void gs_sys_gc_done_with_collection(void);

/* §paragraph{All-in-one GC function for secondary thread} */
/* ↓ Pass §ccode{0} to ignore errors */
int gs_sys_gc_allow_collection(struct gsstringbuilder *);

int gs_sys_block_in_gc_from_space(void *);

typedef void gs_sys_gc_pre_callback(void);
void gs_sys_gc_pre_callback_register(gs_sys_gc_pre_callback *);

typedef int gs_sys_gc_root_callback(struct gsstringbuilder *);
void gs_sys_gc_root_callback_register(gs_sys_gc_root_callback *);

typedef int gs_sys_gc_post_callback(struct gsstringbuilder *);
void gs_sys_gc_post_callback_register(gs_sys_gc_post_callback *);

typedef void gs_sys_gc_failure_callback(void);
void gs_sys_gc_failure_callback_register(gs_sys_gc_failure_callback *);

/* §section ACE */

void ace_up(void);
void ace_down(void);

/* §section API */

struct api_thread;

/* §paragraph{Termination} */
struct api_thread_table {
    enum api_prim_execution_state (*thread_term_status)(struct api_thread *);
    void (*gc_failure_cleanup)(void **);
};

void *api_thread_client_data(struct api_thread *);
void api_take_thread(struct api_thread *);
void api_release_thread(struct api_thread *);

/* ↓ Fetch GC forwarding pointer from thread if needed */
struct api_thread *api_thread_gc_forward(struct api_thread *);

int api_thread_terminating(struct api_thread *);

/* §paragraph{RPCs, for executing part of primitives in the corresponding (or parent) kernel process} */
enum {
    api_std_rpc_done,
    api_std_rpc_abend,
    api_std_rpc_numrpcs,
};
struct api_process_rpc_table {
    char *name;
    int numrpcs;
    gsrpc_handler *handlers[];
};
gsrpc_handler api_main_process_unimpl_rpc;
void api_send_rpc(struct api_thread *, struct gsrpc *);

enum api_prim_execution_state {
    api_st_success,
    api_st_error,
    api_st_blocked,
};

typedef struct api_prim_blocking *api_prim_blocking_gccopy(struct gsstringbuilder *, struct api_prim_blocking *);
typedef int api_prim_blocking_gcevacuate(struct gsstringbuilder *, struct api_prim_blocking *);
typedef void api_prim_blocking_gccleanup(struct api_prim_blocking *);

struct api_prim_blocking {
    struct api_prim_blocking *forward;
    api_prim_blocking_gccopy *gccopy;
    api_prim_blocking_gcevacuate *gcevacuate;
    api_prim_blocking_gccleanup *gccleanup;
};

void *api_blocking_alloc(ulong, api_prim_blocking_gccopy *, api_prim_blocking_gcevacuate *, api_prim_blocking_gccleanup *);

typedef enum api_prim_execution_state (api_prim_executor)(struct api_thread *, struct gseprim *, struct api_prim_blocking **, gsvalue *);

struct api_prim_table {
    int numprims;
    api_prim_executor *execs[];
};

api_prim_executor api_thread_handle_prim_unit;

typedef void (api_termination_callback)(void);
void api_at_termination(api_termination_callback *);

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void apisetupmainthread(struct gspos, struct api_process_rpc_table *, struct api_thread_table *, void *, struct api_prim_table *, gsvalue);

/* If (and only if) the current thread is hard, these will send a done message (§c{api_std_rpc_done}) to the corresponding process */
void api_done(struct api_thread *);

/* If (and only if) the current thread is hard, these will send an abend message (§c{api_std_rpc_abend}) to the corresponding process */
void api_abend(struct api_thread *, char *, ...);
void api_abend_unimpl(struct api_thread *, char *, int, char *, ...);

/* If (and only if) the destination thread is hard, these will send an abend message (§c{api_std_rpc_abend}) to the corresponding process */
void api_thread_post(struct api_thread *, char *, ...);
void api_thread_post_unimpl(struct api_thread *, char *, int, char *, ...);

gsrpc_handler api_main_process_handle_rpc_done;
gsrpc_handler api_main_process_handle_rpc_abend;

/* §section Buffered I/O library */

#ifndef NO_IO_ROUTINES
struct gsbio_dir {
    long size;
    Dir d;
};

struct gsbio_dir *gsbio_stat(char *);
int gsisdir(char *);

struct uxio_ichannel *gsbio_device_iopen(char *, int);
struct uxio_dir_ichannel *gsbio_dir_iopen(char *, int);
struct uxio_ichannel *gsbio_envvar_iopen(char *);
long gsbio_device_iclose(struct uxio_ichannel *);

long gsbio_device_getline(struct uxio_ichannel *, char *, long);
long gsbio_get_contents(struct uxio_ichannel *, char *, long);

struct gsbio_dir *gsbio_read_stat(struct uxio_dir_ichannel *);
struct gsbio_dir *gsbio_parse_stat(u16int, void *);

int gsbio_idevice_at_eof(struct uxio_ichannel *);

#ifdef __UNIX__
long gsbio_unix_read_directory(int, void *, void *, vlong *);
void gsbio_unix_parse_directory(char *, void *, void *, void **, void *, void *, void **);
#endif

#endif

/* §section String Builders */

struct gsstringbuilder {
    struct gsstringbuilder *forward;
    char *end;
    char start[];
};

struct gsstringbuilder *gsreserve_string_builder(void);

int gsextend_string_builder(struct gsstringbuilder *, ulong);
void gsstring_builder_print(struct gsstringbuilder *, char *, ...);
void gsstring_builder_vprint(struct gsstringbuilder *, char *, va_list);

void gsfinish_string_builder(struct gsstringbuilder *);

int gsstring_builder_trace(struct gsstringbuilder *, struct gsstringbuilder **);

#if defined(__cplusplus)
}
#endif
#endif	/* _GLOBALSCRIPT_H_ */
