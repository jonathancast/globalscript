#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "gsregtables.h"
#include "ace.h"

gsvalue gsentrypoint;
struct gstype *gsentrytype;

struct ace_thread_pool_args {
};

static struct ace_thread_queue *ace_thread_queue;

static struct gs_block_class ace_thread_queue_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "ACE Thread Queue",
};
static void *ace_thread_queue_nursury;

static void ace_thread_pool_main(void *);

int
ace_init()
{
    int ace_pool;

    struct ace_thread_pool_args ace_thread_pool_args;

    if (ace_thread_queue)
        gsfatal("ace_init called twice")
    ;

    ace_thread_queue = gs_sys_seg_suballoc(&ace_thread_queue_descr, &ace_thread_queue_nursury, sizeof(*ace_thread_queue), sizeof(void*));

    memset(ace_thread_queue, 0, sizeof(*ace_thread_queue));

    ace_up();

    if ((ace_pool = gscreate_thread_pool(ace_thread_pool_main, &ace_thread_pool_args, sizeof(ace_thread_pool_args))) < 0)
        gsfatal("Couldn't create ACE thread pool: %r")
    ;

    return 0;
}

static
void
ace_thread_pool_main(void *p)
{
    int have_clients, suspended_runnable_thread;
    int i, j;

    have_clients = 1;

    while (have_clients) {
        lock(&ace_thread_queue->lock);

        have_clients = ace_thread_queue->refcount > 0;
        suspended_runnable_thread = 0;
        if (have_clients) {
            for (i = 0; i < NUM_ACE_THREADS; i++) {
                struct ace_thread *thread;

                thread = ace_thread_queue->threads[i];
                if (thread) {
                    struct gsbc *ip;

                    lock(&thread->lock);
                    ip = thread->ip;
                    switch (ip->instr) {
                        case gsbc_op_record:
                            {
                                struct gsheap_item *hp;
                                struct gsrecord *record;

                                hp = (struct gsheap_item *)thread->base;

                                record = gsreserverecords(sizeof(*record) + ip->args[0] * sizeof(gsvalue));
                                record->pos = ip->pos;
                                record->numfields = ip->args[0];
                                for (j = 0; j < ip->args[0]; j++) {
                                    lock(&hp->lock);
                                    gspoison_unimpl(hp, __FILE__, __LINE__, ip->pos, ".record fields");
                                    unlock(&hp->lock);
                                    break;
                                }

                                if (thread->nregs >= MAX_NUM_REGISTERS) {
                                    lock(&hp->lock);
                                    gspoison_unimpl(hp, __FILE__, __LINE__, ip->pos, "Too many registers");
                                    unlock(&hp->lock);
                                    break;
                                }
                                thread->regs[thread->nregs] = (gsvalue)record;
                                thread->nregs++;
                                thread->ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[0]);
                                suspended_runnable_thread = 1;
                            }
                            break;
                        case gsbc_op_yield:
                            {
                                struct gsheap_item *hp;
                                struct gsindirection *in;

                                if (ip->args[0] >= thread->nregs) {
                                    lock(&hp->lock);
                                    gspoison_unimpl(hp, __FILE__, __LINE__, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
                                    unlock(&hp->lock);
                                    break;
                                }

                                hp = (struct gsheap_item *)thread->base;

                                lock(&hp->lock);

                                hp->type = gsindirection;
                                in = (struct gsindirection *)hp;
                                in->target = thread->regs[ip->args[0]];

                                unlock(&hp->lock);

                                ace_thread_queue->threads[i] = 0;
                            }
                            break;
                        case gsbc_op_undef:
                            {
                                struct gsheap_item *hp;
                                struct gserror *err;
                                struct gsindirection *in;

                                hp = (struct gsheap_item *)thread->base;

                                err = gsreserveerrors(sizeof(*err));
                                err->pos = ip->pos;
                                err->type = gserror_undefined;

                                lock(&hp->lock);

                                hp->type = gsindirection;
                                in = (struct gsindirection *)hp;
                                in->target = (gsvalue)err;

                                unlock(&hp->lock);

                                ace_thread_queue->threads[i] = 0;
                            }
                            break;
                        default:
                            {
                                struct gsheap_item *hp;

                                hp = (struct gsheap_item *)thread->base;

                                lock(&hp->lock);
                                gspoison_unimpl(hp, __FILE__, __LINE__, ip->pos, "run instruction %d", ip->instr);
                                unlock(&hp->lock);

                                ace_thread_queue->threads[i] = 0;
                            }
                            break;
                    }
                    unlock(&thread->lock);
                }
            }
        }

        unlock(&ace_thread_queue->lock);
        if (!suspended_runnable_thread)
            sleep(1)
        ;
    }

    gswarning("%s:%d: ace_thread_pool_main terminating", __FILE__, __LINE__);
}

void ace_up()
{
    lock(&ace_thread_queue->lock);
    ace_thread_queue->refcount++;
    unlock(&ace_thread_queue->lock);
}

void
ace_down()
{
    lock(&ace_thread_queue->lock);
    ace_thread_queue->refcount--;
    unlock(&ace_thread_queue->lock);
}

struct ace_rpc {
    struct gsrpc rpc;
    gsvalue value;
};

gstypecode
ace_start_evaluation(gsvalue val)
{
    struct ace_thread *thread;
    struct gsheap_item *hp;
    struct gsclosure *cl;
    struct gseval *ev;
    struct gsbco *code;
    void *ip;
    struct gsbc *instr;
    int i;

    hp = (struct gsheap_item *)val;
    cl = (struct gsclosure *)hp;

    lock(&ace_thread_queue->lock);
    for (i = 0; i < NUM_ACE_THREADS; i++) {
        if (!ace_thread_queue->threads[i]) {
            thread = ace_thread_queue->threads[i] = ace_thread_alloc();
            unlock(&ace_thread_queue->lock);
            goto have_thread;
        }
    }
    unlock(&ace_thread_queue->lock);
    gswerrstr_unimpl(__FILE__, __LINE__, "oothreads");
    return gstyeoothreads;

have_thread:
    thread->base = val;

    thread->nregs = 0;

    code = cl->code;
    ip = (uchar*)code + sizeof(*code);
    if ((uintptr)ip % sizeof(gsvalue*))
        ip = (uchar*)ip + sizeof(gsvalue*) - (uintptr)ip % sizeof(gsvalue*)
    ;
    for (i = 0; i < code->numglobals; i++) {
        gspoison_unimpl(hp, __FILE__, __LINE__, code->pos, "ace_start_evaluation: copy in global variable");
        ip = (gsvalue*)ip + 1;
    }
    instr = (struct gsbc *)ip;

    thread->ip = instr;

    ev = (struct gseval *)hp;
    hp->type = gseval;
    ev->thread = thread;

    unlock(&thread->lock);

    return gstystack;
}

static Lock ace_thread_lock;
static struct gs_block_class ace_thread_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "ACE Thread",
};
static void *ace_thread_nursury;

struct ace_thread *
ace_thread_alloc()
{
    struct ace_thread *thread;

    lock(&ace_thread_lock);
    thread = gs_sys_seg_suballoc(&ace_thread_descr, &ace_thread_nursury, sizeof(*thread), sizeof(void*));
    unlock(&ace_thread_lock);

    lock(&thread->lock);

    return thread;
}
