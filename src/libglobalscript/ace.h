int ace_init(void);

gstypecode ace_start_evaluation(gsvalue);

extern gsvalue gsentrypoint;
extern struct gstype *gsentrytype;

#define NUM_ACE_THREADS 0x70

struct ace_thread_queue {
    Lock lock;
    int refcount;
    vlong numthreads;
    uint num_active_threads;
    struct ace_thread *threads[NUM_ACE_THREADS];
};

struct ace_thread *ace_thread_alloc(void);

struct ace_thread {
    Lock lock;
    int tid;
    struct gsbc_cont_update *cureval;
    enum {
        ace_thread_running,
        ace_thread_blocked,
        ace_thread_lprim_blocked,
        ace_thread_finished,
        ace_thread_gcforward,
    } state;
    union {
        struct {
            struct gsbco *bco;
            struct gsbc *ip;
        } running;
        struct {
            gsvalue on;
            struct gspos at;
        } blocked;
        struct {
            struct gslprim_blocking *on;
            struct gspos at;
        } lprim_blocked;
        struct {
            struct ace_thread *dest;
        } forward;
    } st;
    int nregs, nsubexprs;
    struct gsbco *subexprs[MAX_NUM_REGISTERS];
    gsvalue regs[MAX_NUM_REGISTERS];
    void *stacklimit, *stacktop, *stackbot;
};

void ace_thread_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);

int ace_thread_gc_trace(struct gsstringbuilder *, struct ace_thread **);
