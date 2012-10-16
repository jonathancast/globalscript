int ace_init(void);

gstypecode ace_start_evaluation(gsvalue);

extern gsvalue gsentrypoint;

#define NUM_ACE_THREADS 0x100

struct ace_thread_queue {
    Lock lock;
    int refcount;
    struct ace_thread *threads[NUM_ACE_THREADS];
};

struct ace_thread *ace_thread_alloc(void);

struct ace_thread {
    Lock lock;
    gsvalue base;
    struct gsbc *ip;
};
