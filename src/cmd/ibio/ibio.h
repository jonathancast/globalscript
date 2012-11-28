/* §section RPCs for Unix Processes */

enum {
    ibio_uxproc_rpc_stat = api_std_rpc_numrpcs,
    ibio_numrpcs,
};

/* §section Threads */

struct ibio_thread_data {
    gsvalue cmd_args;
    struct ibio_thread_to_oport_link *writing_to_oport;
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
