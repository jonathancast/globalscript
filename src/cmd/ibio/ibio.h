/* §section Threads */

struct ibio_thread_data {
    struct ibio_thread_to_oport_link *writing_to_oport;
};

void *ibio_main_thread_alloc_data(void);
enum api_prim_execution_state ibio_thread_term_status(struct api_thread *);

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
