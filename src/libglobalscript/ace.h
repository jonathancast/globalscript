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
    gsvalue blocked;
    struct gspos blockedat;
    struct gsbc *ip;
    int nregs;
    gsvalue regs[MAX_NUM_REGISTERS];
    void *stacklimit, *stacktop, *stackbot;
};
