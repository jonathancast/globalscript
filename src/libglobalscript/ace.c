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

static struct gsbc_cont *ace_stack_alloc(struct ace_thread *, struct gspos, ulong);

static void ace_error_thread(struct ace_thread *, struct gserror *);
static void ace_poison_thread(struct ace_thread *, struct gspos, char *, ...);
static void ace_poison_thread_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);
static int ace_return(struct ace_thread *, struct gspos, gsvalue);

static int ace_alloc_record(struct ace_thread *, struct gsbc *);

static
void
ace_thread_pool_main(void *p)
{
    int have_clients, suspended_runnable_thread;
    int i, j;

    have_clients = 1;

    while (have_clients) {
        have_clients = ace_thread_queue->refcount > 0;
        suspended_runnable_thread = 0;
        if (have_clients) {
            for (i = 0; i < NUM_ACE_THREADS; i++) {
                struct ace_thread *thread;

                thread = 0;
                lock(&ace_thread_queue->lock);
                while (i < NUM_ACE_THREADS && !thread) {
                    thread = ace_thread_queue->threads[i];
                    if (!thread) i++;
                }
                unlock(&ace_thread_queue->lock);

                if (thread) {
                    struct gsbc *ip;

                    lock(&thread->lock);
                    ip = thread->ip;
                    switch (ip->instr) {
                        case gsbc_op_record:
                            if (ace_alloc_record(thread, thread->ip))
                                suspended_runnable_thread = 1
                            ;
                            break;
                        case gsbc_op_unknown_eprim:
                            {
                                struct gseprim *prim;

                                prim = gsreserveeprims(sizeof(*prim));
                                prim->pos = ip->pos;
                                prim->index = -1;

                                if (thread->nregs >= MAX_NUM_REGISTERS) {
                                    ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
                                    break;
                                }
                                thread->regs[thread->nregs] = (gsvalue)prim;
                                thread->nregs++;
                                thread->ip = GS_NEXT_BYTECODE(ip, 0);
                                suspended_runnable_thread = 1;
                            }
                            break;
                        case gsbc_op_eprim:
                            {
                                struct gseprim *prim;

                                prim = gsreserveeprims(sizeof(*prim) + ip->args[1] * sizeof(gsvalue));
                                prim->pos = ip->pos;
                                prim->index = ip->args[0];
                                for (j = 0; j < ip->args[1]; j++) {
                                    if (ip->args[2 + j] >= thread->nregs) {
                                        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".eprim argument too large");
                                        goto eprim_failed;
                                    }
                                    prim->arguments[j] = thread->regs[ip->args[2 + j]];
                                }

                                if (thread->nregs >= MAX_NUM_REGISTERS) {
                                    ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
                                    break;
                                }
                                thread->regs[thread->nregs] = (gsvalue)prim;
                                thread->nregs++;
                                thread->ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);
                                suspended_runnable_thread = 1;
                            }
                        eprim_failed:
                            break;
                        case gsbc_op_app:
                            {
                                struct gsbc_cont *cont;
                                struct gsbc_cont_app *app;

                                cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont) + ip->args[0] * sizeof(gsvalue));
                                app = (struct gsbc_cont_app *)cont;
                                if (!cont) goto app_failed;

                                cont->node = gsbc_cont_app;
                                cont->pos = ip->pos;
                                app->numargs = ip->args[0];
                                for (j = 0; j < ip->args[0]; j++) {
                                    if (ip->args[1 + j] >= thread->nregs) {
                                        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".app argument too large");
                                        goto app_failed;
                                    }
                                    app->arguments[j] = thread->regs[ip->args[1 + j]];
                                }

                                thread->ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[1]);
                                suspended_runnable_thread = 1;
                            }
                        app_failed:
                            break;
                        case gsbc_op_undef:
                            {
                                struct gserror *err;

                                err = gsreserveerrors(sizeof(*err));
                                err->pos = ip->pos;
                                err->type = gserror_undefined;

                                ace_error_thread(thread, err);
                            }
                            break;
                        case gsbc_op_enter:
                            {
                                gstypecode st;
                                gsvalue prog;

                                if (ip->args[0] >= thread->nregs) {
                                    ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
                                    break;
                                }

                                prog = thread->regs[ip->args[0]];
                                st = GS_SLOW_EVALUATE(prog);

                                switch (st) {
                                    case gstystack:
                                        break;
                                    case gstyindir:
                                        if (ace_return(thread, ip->pos, gsremove_indirections(prog)) > 0)
                                            suspended_runnable_thread = 1
                                        ;
                                        break;
                                    default:
                                        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter (st = %d)", st);
                                        break;
                                }
                            }
                            break;
                        case gsbc_op_yield:
                            {
                                if (ip->args[0] >= thread->nregs) {
                                    ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
                                    break;
                                }

                                if (ace_return(thread, ip->pos, thread->regs[ip->args[0]]) > 0)
                                    suspended_runnable_thread = 1
                                ;
                            }
                            break;
                        default:
                            {
                                ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "run instruction %d", ip->instr);
                            }
                            break;
                    }
                    unlock(&thread->lock);
                }
            }
        }

        if (!suspended_runnable_thread)
            sleep(1)
        ;
    }

    gswarning("%s:%d: ace_thread_pool_main terminating", __FILE__, __LINE__);
}

static
int
ace_alloc_record(struct ace_thread *thread, struct gsbc *ip)
{
    struct gsrecord *record;
    int j;

    record = gsreserverecords(sizeof(*record) + ip->args[0] * sizeof(gsvalue));
    record->pos = ip->pos;
    record->numfields = ip->args[0];
    for (j = 0; j < ip->args[0]; j++) {
        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".record fields");
        return 0;
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs] = (gsvalue)record;
    thread->nregs++;
    thread->ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[0]);
    return 1;
}

static
struct gsbc_cont *
ace_stack_alloc(struct ace_thread *thread, struct gspos pos, ulong sz)
{
    void *newtop;

    newtop = (uchar*)thread->stacktop - sz;
    if ((uintptr)newtop % sizeof(gsvalue)) {
        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack mis-aligned (can't round down or we couldn't pop properly)");
        return 0;
    }

    if ((uchar*)newtop < (uchar*)thread->stacklimit) {
        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack overflow");
        return 0;
    }

    thread->stacktop = newtop;

    return (struct gsbc_cont *)newtop;
}

static void gsupdate_heap(struct gsheap_item *, gsvalue);
static void ace_remove_thread(struct ace_thread *);

static
void
ace_error_thread(struct ace_thread *thread, struct gserror *err)
{
    struct gsheap_item *hp;

    hp = (struct gsheap_item *)thread->base;

    lock(&hp->lock);
    gsupdate_heap(hp, (gsvalue)err);
    unlock(&hp->lock);

    ace_remove_thread(thread);
}

static
void
ace_poison_thread(struct ace_thread *thread, struct gspos srcpos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf + sizeof(buf), fmt, arg);
    va_end(arg);

    ace_error_thread(thread, gserror(srcpos, "%s", buf));
}

static
void
ace_poison_thread_unimpl(struct ace_thread *thread, char *file, int lineno, struct gspos srcpos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf + sizeof(buf), fmt, arg);
    va_end(arg);

    ace_error_thread(thread, gserror_unimpl(file, lineno, srcpos, "%s", buf));
}

static void *ace_set_registers(struct ace_thread *, struct gsclosure *);

static
int
ace_return(struct ace_thread *thread, struct gspos srcpos, gsvalue v)
{
    if ((uchar*)thread->stacktop >= (uchar*)thread->stackbot) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)thread->base;

        lock(&hp->lock);
        gsupdate_heap(hp, v);
        unlock(&hp->lock);

        ace_remove_thread(thread);
        return 0;
    } else {
        struct gsbc_cont *cont;

        cont = (struct gsbc_cont *)thread->stacktop;
        switch (cont->node) {
            case gsbc_cont_app: {
                struct gs_blockdesc *block;
                struct gsheap_item *fun;
                struct gsclosure *cl;
                struct gsbc_cont_app *app;
                int needed_args;

                app = (struct gsbc_cont_app *)cont;

                block = BLOCK_CONTAINING(v);
                if (!gsisheap_block(block)) {
                    ace_poison_thread(thread, cont->pos, "Applied function is a %s not a closure", block->class->description);
                    return 0;
                }

                fun = (struct gsheap_item *)v;

                lock(&fun->lock);

                if (fun->type != gsclosure) {
                    ace_poison_thread(thread, cont->pos, "Applied function %P is not a closure", fun->pos);
                    unlock(&fun->lock);
                    return 0;
                }

                cl = (struct gsclosure *)fun;
                needed_args = cl->code->numargs - (cl->numfvs - cl->code->numfvs);

                if (app->numargs < needed_args) {
                    ace_poison_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Partial applications");
                    unlock(&fun->lock);
                    return 0;
                } else if (app->numargs > needed_args) {
                    ace_poison_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Over-saturated applications");
                    unlock(&fun->lock);
                    return 0;
                } else {
                    int i;
                    void *ip;

                    ip = ace_set_registers(thread, cl);
                    if (!ip) {
                        ace_poison_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                        unlock(&fun->lock);
                        return 0;
                    }

                    for (i = 0; i < app->numargs; i++) {
                        if (thread->nregs >= MAX_NUM_REGISTERS) {
                            ace_poison_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                            unlock(&fun->lock);
                            return 0;
                        }
                        thread->regs[thread->nregs] = app->arguments[i];
                        thread->nregs++;
                    }
                    thread->ip = (struct gsbc *)ip;
                }

                unlock(&fun->lock);

                return 1;
            }
            default:
                ace_poison_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Return to %d continuations", cont->node);
                return 0;
        }
    }
}

static
void
gsupdate_heap(struct gsheap_item *hp, gsvalue v)
{
    struct gsindirection *in;

    hp->type = gsindirection;
    in = (struct gsindirection *)hp;
    in->target = v;
}

static
void
ace_remove_thread(struct ace_thread *thread)
{
    lock(&ace_thread_queue->lock);
    ace_thread_queue->threads[thread->tid] = 0;
    unlock(&ace_thread_queue->lock);
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
    struct gsbc *instr;
    int i;

    hp = (struct gsheap_item *)val;
    cl = (struct gsclosure *)hp;

    thread = ace_thread_alloc();

    thread->base = val;

    instr = (struct gsbc *)ace_set_registers(thread, cl);

    if (!instr) {
        gspoison_unimpl(hp, __FILE__, __LINE__, cl->code->pos, "Too many registers");
        unlock(&thread->lock);
        return gstywhnf;
    }

    thread->ip = instr;

    ev = (struct gseval *)hp;
    hp->type = gseval;
    ev->thread = thread;

    unlock(&thread->lock);

    lock(&ace_thread_queue->lock);
    for (i = 0; i < NUM_ACE_THREADS; i++) {
        if (!ace_thread_queue->threads[i]) {
            ace_thread_queue->threads[i] = thread;
            unlock(&ace_thread_queue->lock);
            thread->tid = i;
            return gstystack;
        }
    }
    unlock(&ace_thread_queue->lock);
    gswerrstr_unimpl(__FILE__, __LINE__, "oothreads");
    return gstyeoothreads;
}

static
void *
ace_set_registers(struct ace_thread *thread, struct gsclosure *cl)
{
    void *ip;
    struct gsbco *code;
    int i;

    thread->nregs = 0;

    code = cl->code;
    ip = (uchar*)code + sizeof(*code);
    if ((uintptr)ip % sizeof(gsvalue*))
        ip = (uchar*)ip + sizeof(gsvalue*) - (uintptr)ip % sizeof(gsvalue*)
    ;
    for (i = 0; i < code->numglobals; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            return 0;
        }
        thread->regs[thread->nregs] = *(gsvalue*)ip;
        thread->nregs++;
        ip = (gsvalue*)ip + 1;
    }
    return ip;
}

static Lock ace_thread_lock;
static struct gs_block_class ace_thread_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "ACE Thread",
};
static void *ace_thread_nursury;

#define ACE_STACK_ARENA_SIZE (sizeof(gsvalue) * 0x400)

struct ace_thread *
ace_thread_alloc()
{
    struct gs_blockdesc *nursury_block;
    struct ace_thread *thread;
    void *stackbase, *stackbot;

    lock(&ace_thread_lock);
        thread = gs_sys_seg_suballoc(&ace_thread_descr, &ace_thread_nursury, sizeof(*thread), sizeof(void*));
        nursury_block = START_OF_BLOCK(thread);
        stackbase = (uchar*)thread;
        if ((uintptr)stackbase % ACE_STACK_ARENA_SIZE)
            stackbase = (uchar*)stackbase - ((uintptr)stackbase % ACE_STACK_ARENA_SIZE)
        ;
        stackbot = (uchar*)stackbase + ACE_STACK_ARENA_SIZE;
        if ((uchar*)stackbot >= (uchar*)END_OF_BLOCK(nursury_block))
            ace_thread_nursury = 0
        ; else
            ace_thread_nursury = stackbot
        ;
    unlock(&ace_thread_lock);

    lock(&thread->lock);

    thread->stackbot = stackbot;
    thread->stacktop = stackbot;
    thread->stacklimit = (uchar*)thread + sizeof(*thread);

    return thread;
}
