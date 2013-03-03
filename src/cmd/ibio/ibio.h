/* §section Interface Down to GS */

enum ibio_gsstring_eval_state {
    ibio_gsstring_eval_error,
    ibio_gsstring_eval_blocked,
    ibio_gsstring_eval_success,
};

struct ibio_gsstring_eval {
    gsvalue gss, gsc;
    struct gsstringbuilder sb;
};

void ibio_gsstring_eval_start(struct ibio_gsstring_eval *, gsvalue);
enum ibio_gsstring_eval_state ibio_gsstring_eval_advance(struct api_thread *, struct gspos, struct ibio_gsstring_eval *);

/* §section RPCs for Unix Processes */

enum {
    ibio_uxproc_rpc_stat = api_std_rpc_numrpcs,
    ibio_uxproc_rpc_file_read_open,
    ibio_numrpcs,
};

/* §section Threads */

struct ibio_thread_data {
    gsvalue cmd_args;
    struct ibio_thread_to_oport_link *writing_to_oport;
    struct ibio_thread_to_iport_link *reading_from_iport;
};

void *ibio_main_thread_alloc_data(struct gspos, int, char **);
enum api_prim_execution_state ibio_thread_term_status(struct api_thread *);

/* §section Environment */

api_prim_executor ibio_handle_prim_getargs;

/* §section Stat */

#define IBIO_DIR_TYPE \
    "bool.t " \
    "list.t rune.t ` " \
    "\"Π〈 " \
        "mode.directory " \
        "name " \
    "〉" \

enum {
    ibio_stat_mode_directory,
    ibio_stat_name,
    ibio_stat_num_fields,
};

gsvalue ibio_parse_gsbio_dir(struct gspos, struct gsbio_dir *dir);

api_prim_executor ibio_handle_prim_file_stat;

gsrpc_handler ibio_main_process_handle_rpc_stat;

/* §section Channels */

struct ibio_channel_segment {
    Lock lock;
    struct ibio_iport *iport;
    struct ibio_channel_segment *next;
    gsvalue *extent;
    gsvalue items[];
};

struct ibio_channel_segment *ibio_channel_segment_containing(gsvalue *);

struct ibio_channel_segment *ibio_alloc_channel_segment(void);

gsvalue *ibio_channel_segment_limit(struct ibio_channel_segment *);

/* §section File I/O */

struct ibio_external_io;

typedef int ibio_external_canread(struct ibio_external_io *, void *, void *);
typedef void *ibio_external_readsym(struct ibio_external_io *, char *, char *, void *, void *, gsvalue *);

struct ibio_external_io {
    ibio_external_canread *canread;
    ibio_external_readsym *readsym;
};

struct ibio_external_io *ibio_rune_io(void);
struct ibio_external_io *ibio_dir_io(struct gspos, gsvalue);

struct ibio_uxio;

typedef long ibio_uxio_refill(struct ibio_uxio *, int, void *, long);

struct ibio_uxio {
    ibio_uxio_refill *refill;
};

struct ibio_uxio *ibio_file_uxio(void);
struct ibio_uxio *ibio_dir_uxio(char *);

gsprim_handler ibio_prim_external_io_handle_rune, ibio_prim_external_io_handle_dir;

api_prim_executor ibio_handle_prim_file_read_open;

gsrpc_handler ibio_main_process_handle_rpc_file_read_open;

/* §section Input */

void ibio_check_acceptor_type(struct gspos, struct gsfile_symtable *);

struct ibio_iport {
    Lock lock;
    int active;
    struct ibio_channel_segment *last_accessed_seg;
    gsvalue *position;
    gsvalue reading;
    struct api_thread *reading_thread;
    struct ibio_iport_read_blocker *waiting_to_read, **waiting_to_read_end;
    gsvalue error;
    /* §section Used for §gs{iport}s connected to external files */
    int fd;
    struct ibio_uxio *uxio;
    struct ibio_external_io *external;
    void *buf, *bufstart, *bufend, *bufextent;
};

int ibio_read_threads_init(char *, char *);

gsvalue ibio_iport_fdopen(int, struct ibio_uxio *, struct ibio_external_io *, char *, char *);

api_prim_executor ibio_handle_prim_read;

gsubprim_handler ibio_prim_iptr_handle_iseof;
gslprim_handler ibio_prim_iptr_handle_deref, ibio_prim_iptr_handle_next;

/* §section Output */

struct ibio_oport {
    Lock lock;
    int active;
    gsvalue writing;
    struct ibio_oport_write_blocker *waiting_to_write, **waiting_to_write_end;
    struct api_thread *writing_thread;
    /* §section Used for §gs{oport}s connected to external files */
    int fd;
    void *buf, *bufend, *bufextent;
};

gsvalue ibio_oport_fdopen(int, char *, char *);

api_prim_executor ibio_handle_prim_write;

int ibio_write_threads_init(char *, char *);
