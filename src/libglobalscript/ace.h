int ace_init(void);

gstypecode ace_start_evaluation(struct gspos, struct gsheap_item *);

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
        ace_thread_entering_bco,
        ace_thread_returning,
        ace_thread_running,
        ace_thread_blocked,
        ace_thread_lprim_blocked,
        ace_thread_finished,
        ace_thread_gcforward,
    } state;
    union {
        struct {
            struct gsbco *bco;
        } entering_bco;
        struct {
            struct gspos from;
        } returning;
        struct {
            struct gspos at;
            gsvalue on;
        } blocked;
        struct {
            struct gspos at;
            struct gslprim_blocking *on;
        } lprim_blocked;
        struct {
            struct ace_thread *dest;
        } forward;
    } st;
    void *stacklimit, *stacktop, *stackbot;
    void *gc_evacuated_stackbot;
    vlong blocked_time;
};

void ace_failure_thread(struct ace_thread *, struct gsimplementation_failure *);
void ace_thread_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);

int ace_eval_gc_trace(struct gsstringbuilder *, struct gsbc_cont_update **);

int ace_stack_gcevacuate(struct gsstringbuilder *, struct ace_thread *, struct gsbc_cont_update *);
