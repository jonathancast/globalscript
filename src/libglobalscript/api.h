#define API_NUMTHREADS 0x100

struct api_thread {
    struct Lock lock;
    int hard;
    vlong start_time, prog_term_time;
    enum {
        api_thread_st_unused,
        api_thread_st_active,
        api_thread_st_terminating_on_done,
        api_thread_st_terminating_on_abend,
        api_thread_st_zombie,
    } state;
    struct gsrpc_queue *process_rpc_queue;
    struct api_thread_table *api_thread_table;
    struct api_prim_table *api_prim_table;
    void *client_data;
    char *status;
    struct api_code_segment *code;
    struct api_prim_blocking *eprim_blocking;
};

struct api_instr {
    gsvalue instr;
    struct api_promise *presult;
};

struct api_code_segment {
    int size, ip;
    struct api_code_segment *fwd;
    struct api_instr instrs[];
};

struct api_promise {
    Lock lock;
    gsvalue value;
};

struct api_thread_queue {
    struct Lock lock;
    int numthreads;
    struct api_thread threads[API_NUMTHREADS];
};
