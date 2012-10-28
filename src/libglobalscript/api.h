#define API_NUMTHREADS 0x100

struct api_thread {
    struct Lock lock;
    int active, hard;
    struct gsrpc_queue *process_rpc_queue;
    struct api_prim_table *api_prim_table;
    struct api_code_segment *code;
};

struct api_instr {
    gsvalue instr;
    struct api_promise *presult;
};

struct api_code_segment {
    int size, ip;
    struct api_instr instrs[];
};

struct api_promise {
    Lock lock;
    gsvalue value;
};

struct api_thread_queue {
    struct Lock lock;
    struct api_thread threads[API_NUMTHREADS];
};
