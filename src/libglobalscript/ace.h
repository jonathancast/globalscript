int ace_init(void);

gstypecode ace_start_evaluation(gsvalue);

struct gsfile_symtable *gscurrent_symtable;

extern gsvalue gsentrypoint;
extern struct gstype *gsentrytype;

#define NUM_ACE_THREADS 0x100

struct ace_thread_queue {
    Lock lock;
    int refcount;
    struct ace_thread *threads[NUM_ACE_THREADS];
};

struct ace_thread *ace_thread_alloc(void);

struct ace_thread {
    Lock lock;
    int tid;
    gsvalue base;
    enum {
        ace_thread_running,
        ace_thread_blocked,
        ace_thread_lprim_blocked,
    } state;
    union {
        struct {
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
    } st;
    int nregs, nsubexprs;
    struct gsbco *subexprs[MAX_NUM_REGISTERS];
    gsvalue regs[MAX_NUM_REGISTERS];
    void *stacklimit, *stacktop, *stackbot;
};

void ace_thread_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);
