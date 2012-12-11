/* §section RPCs for Unix Processes */

enum {
    ibio_uxproc_rpc_stat = api_std_rpc_numrpcs,
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
    "\"Π〈 " \
        "mode.directory " \
    "〉" \

enum {
    ibio_stat_mode_directory,
    ibio_stat_num_fields,
};

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

/* §section Input */

void ibio_check_acceptor_type(char *, struct gsfile_symtable *, struct gspos);

struct ibio_iport {
    Lock lock;
    int active;
    gsvalue *position;
    gsvalue reading;
    struct api_thread *reading_thread;
    gsvalue error;
    /* §section Used for §gs{iport}s connected to external files */
    int fd;
    long (*refill)(int, void *, long);
    struct ibio_external_io *external;
    void *buf, *bufstart, *bufend, *bufextent;
};

int ibio_read_threads_init(char *, char *);

gsvalue ibio_iport_fdopen(int, struct ibio_external_io *, char *, char *);

api_prim_executor ibio_handle_prim_read;

gsubprim_handler ibio_prim_iptr_handle_iseof;
gslprim_handler ibio_prim_iptr_handle_deref;

/* §section Output */

struct ibio_oport {
    Lock lock;
    int active;
    gsvalue writing;
    struct api_thread *writing_thread;
    /* §section Used for §gs{oport}s connected to external files */
    int fd;
    void *buf, *bufend, *bufextent;
};

gsvalue ibio_oport_fdopen(int, char *, char *);

api_prim_executor ibio_handle_prim_write;

int ibio_write_threads_init(char *, char *);
