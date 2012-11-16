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
    /* indirection_dereferencer = */ gsnoindir,
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

static void ace_thread_unimpl(struct ace_thread *, char *, int, struct gspos, char *, ...);
static int ace_return(struct ace_thread *, struct gspos, gsvalue);
static void ace_error_thread(struct ace_thread *, struct gserror *);

static int ace_alloc_thunk(struct ace_thread *);
static int ace_alloc_constr(struct ace_thread *);
static int ace_alloc_record(struct ace_thread *);
static int ace_alloc_unknown_eprim(struct ace_thread *);
static int ace_alloc_eprim(struct ace_thread *);
static int ace_push_app(struct ace_thread *);
static int ace_push_force(struct ace_thread *);
static int ace_perform_analyze(struct ace_thread *);
static void ace_return_undef(struct ace_thread *);
static int ace_enter(struct ace_thread *);
static int ace_yield(struct ace_thread *);

static
void
ace_thread_pool_main(void *p)
{
    int have_clients, suspended_runnable_thread;
    int i;

    have_clients = 1;

    while (have_clients) {
        lock(&ace_thread_queue->lock);
        have_clients = ace_thread_queue->refcount > 0;
        unlock(&ace_thread_queue->lock);
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
                    lock(&thread->lock);

                    if (thread->blocked) {
                        gsvalue prog;
                        gstypecode st;

                        prog = thread->blocked;
                        st = GS_SLOW_EVALUATE(prog);

                        switch (st) {
                            case gstystack:
                                break;
                            case gstyindir:
                                prog = GS_REMOVE_INDIRECTIONS(prog);
                                /* Fall through */
                            case gstywhnf: {
                                if (ace_return(thread, thread->blockedat, prog) > 0)
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            }
                            case gstyerr: {
                                struct gs_blockdesc *block;

                                block = BLOCK_CONTAINING(prog);
                                if (gsiserror_block(block)) {
                                    struct gserror *err;

                                    err = (struct gserror *)prog;
                                    ace_error_thread(thread, err);
                                } else {
                                    ace_thread_unimpl(thread, __FILE__, __LINE__, thread->blockedat, ".enter (%s)", block->class->description);
                                    break;
                                }
                                break;
                            }
                            default:
                                ace_thread_unimpl(thread, __FILE__, __LINE__, thread->blockedat, ".enter (st = %d)", st);
                                break;
                        }
                    } else {
                        switch (thread->ip->instr) {
                            case gsbc_op_alloc:
                                if (ace_alloc_thunk(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_constr:
                                if (ace_alloc_constr(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_record:
                                if (ace_alloc_record(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_unknown_eprim:
                                if (ace_alloc_unknown_eprim(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_eprim:
                                if (ace_alloc_eprim(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_app:
                                if (ace_push_app(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_force:
                                if (ace_push_force(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_analyze:
                                if (ace_perform_analyze(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_undef:
                                ace_return_undef(thread);
                                break;
                            case gsbc_op_enter:
                                if (ace_enter(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            case gsbc_op_yield:
                                if (ace_yield(thread))
                                    suspended_runnable_thread = 1
                                ;
                                break;
                            default:
                                ace_thread_unimpl(thread, __FILE__, __LINE__, thread->ip->pos, "run instruction %d", thread->ip->instr);
                                break;
                        }
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
ace_alloc_thunk(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsheap_item *hp;
    struct gsclosure *cl;
    int i;

    ip = thread->ip;

    hp = gsreserveheap(sizeof(struct gsclosure) + ip->args[1] * sizeof(gsvalue));
    cl = (struct gsclosure *)hp;

    memset(&hp->lock, 0, sizeof(hp->lock));
    hp->pos = ip->pos;
    hp->type = gsclosure;
    if (ip->args[0] > thread->nsubexprs) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".alloc subexpr out of range");
        return 0;
    }
    cl->code = thread->subexprs[ip->args[0]];
    cl->numfvs = ip->args[1];
    for (i = 0; i < ip->args[1]; i++) {
        if (ip->args[2 + i] > thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".alloc free variable out of range");
            return 0;
        }
        cl->fvs[i] = thread->regs[ip->args[2 + i]];
    }
    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Register overflow in .alloc");
        return 0;
    }
    thread->regs[thread->nregs] = (gsvalue)hp;
    thread->nregs++;
    thread->ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);
    return 1;
}

static
int
ace_alloc_constr(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr *constr;
    int i;

    ip = thread->ip;

    constr = gsreserveconstrs(sizeof(*constr) + ACE_CONSTR_NUMARGS(ip) * sizeof(gsvalue));
    constr->pos = ip->pos;
    constr->constrnum = ACE_CONSTR_CONSTRNUM(ip);
    constr->numargs = ACE_CONSTR_NUMARGS(ip);
    for (i = 0; i < ACE_CONSTR_NUMARGS(ip); i++)
        constr->arguments[i] = thread->regs[ACE_CONSTR_ARG(ip, i)]
    ;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs++] = (gsvalue)constr;

    thread->ip = ACE_CONSTR_SKIP(ip);

    return 1;
}

static
int
ace_alloc_record(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsrecord *record;
    int j;

    ip = thread->ip;

    record = gsreserverecords(sizeof(*record) + ip->args[0] * sizeof(gsvalue));
    record->pos = ip->pos;
    record->numfields = ip->args[0];
    for (j = 0; j < ip->args[0]; j++) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".record fields");
        return 0;
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs] = (gsvalue)record;
    thread->nregs++;
    thread->ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[0]);
    return 1;
}

static
int
ace_alloc_unknown_eprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;

    ip = thread->ip;

    prim = gsreserveeprims(sizeof(*prim));
    prim->pos = ip->pos;
    prim->index = -1;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs] = (gsvalue)prim;
    thread->nregs++;
    thread->ip = GS_NEXT_BYTECODE(ip, 0);
    return 1;
}

static
int
ace_alloc_eprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;
    int j;

    ip = thread->ip;

    prim = gsreserveeprims(sizeof(*prim) + ip->args[1] * sizeof(gsvalue));
    prim->pos = ip->pos;
    prim->index = ip->args[0];
    for (j = 0; j < ip->args[1]; j++) {
        if (ip->args[2 + j] >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".eprim argument too large");
            return 0;
        }
        prim->arguments[j] = thread->regs[ip->args[2 + j]];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs] = (gsvalue)prim;
    thread->nregs++;
    thread->ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);

    return 1;
}

static struct gsbc_cont *ace_stack_alloc(struct ace_thread *, struct gspos, ulong);

static
int
ace_push_app(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
    struct gsbc_cont_app *app;
    int j;

    ip = thread->ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_app) + ip->args[0] * sizeof(gsvalue));
    app = (struct gsbc_cont_app *)cont;
    if (!cont) return 0;

    cont->node = gsbc_cont_app;
    cont->pos = ip->pos;
    app->numargs = ip->args[0];
    for (j = 0; j < ip->args[0]; j++) {
        if (ip->args[1 + j] >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".app argument too large");
            return 0;
        }
        app->arguments[j] = thread->regs[ip->args[1 + j]];
    }

    thread->ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[0]);
    return 1;
}

static
int
ace_push_force(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
    struct gsbc_cont_force *force;
    int i;

    ip = thread->ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_force) + ip->args[1] * sizeof(gsvalue));
    force = (struct gsbc_cont_force *)cont;
    if (!cont) return 0;

    cont->node = gsbc_cont_force;
    cont->pos = ip->pos;
    force->code = thread->subexprs[ip->args[0]];
    force->numfvs = ip->args[1];
    for (i = 0; i < ip->args[1]; i++) {
        force->fvs[i] = thread->regs[ip->args[2+i]];
    }

    thread->ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);
    return 1;
}

static void ace_poison_thread(struct ace_thread *, struct gspos, char *, ...);

static
int
ace_perform_analyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr *constr;
    struct gsbc **cases;

    int i;

    ip = thread->ip;

    constr = (struct gsconstr *)thread->regs[ACE_ANALYZE_SCRUTINEE(ip)];
    cases = ACE_ANALYZE_CASES(ip);

    thread->ip = ip = cases[constr->constrnum];
    for (i = 0; i < constr->numargs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_poison_thread(thread, ip->pos, "Register overflow");
            return 0;
        }
        thread->regs[thread->nregs++] = constr->arguments[i];
    }

    return 1;
}

static
void
ace_return_undef(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gserror *err;

    ip = thread->ip;

    err = gsreserveerrors(sizeof(*err));
    err->pos = ip->pos;
    err->type = gserror_undefined;

    ace_error_thread(thread, err);
}

static
int
ace_enter(struct ace_thread *thread)
{
    struct gsbc *ip;
    gstypecode st;
    gsvalue prog;

    ip = thread->ip;

    if (ip->args[0] >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
        return 0;
    }

    prog = thread->regs[ip->args[0]];

    for (;;) {
        st = GS_SLOW_EVALUATE(prog);

        switch (st) {
            case gstystack:
                thread->blocked = prog;
                thread->blockedat = ip->pos;
                return 0;
            case gstyindir:
                prog = GS_REMOVE_INDIRECTIONS(prog);
                break;
            case gstywhnf:
                if (ace_return(thread, ip->pos, prog) > 0)
                    return 1
                ;
                return 0;
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter (st = %d)", st);
                return 0;
        }
    }
}

static
int
ace_yield(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->ip;

    if (ip->args[0] >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
        return 0;
    }

    if (ace_return(thread, ip->pos, thread->regs[ip->args[0]]) > 0)
        return 1
    ;

    return 0;
}

static
struct gsbc_cont *
ace_stack_alloc(struct ace_thread *thread, struct gspos pos, ulong sz)
{
    void *newtop;

    newtop = (uchar*)thread->stacktop - sz;
    if ((uintptr)newtop % sizeof(gsvalue)) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack mis-aligned (can't round down or we couldn't pop properly)");
        return 0;
    }

    if ((uchar*)newtop < (uchar*)thread->stacklimit) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack overflow");
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

static void ace_failure_thread(struct ace_thread *, struct gsimplementation_failure *);

static
void
ace_thread_unimpl(struct ace_thread *thread, char *file, int lineno, struct gspos srcpos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf + sizeof(buf), fmt, arg);
    va_end(arg);

    ace_failure_thread(thread, gsunimpl(file, lineno, srcpos, "%s", buf));
}

static
void
ace_failure_thread(struct ace_thread *thread, struct gsimplementation_failure *err)
{
    struct gsheap_item *hp;

    hp = (struct gsheap_item *)thread->base;

    lock(&hp->lock);
    gsupdate_heap(hp, (gsvalue)err);
    unlock(&hp->lock);

    ace_remove_thread(thread);
}

static void *ace_set_registers_from_closure(struct ace_thread *, struct gsclosure *);

static int ace_return_to_app(struct ace_thread *, struct gsbc_cont *, gsvalue);
static int ace_return_to_force(struct ace_thread *, struct gsbc_cont *, gsvalue);

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
            case gsbc_cont_app:
                return ace_return_to_app(thread, cont, v);
            case gsbc_cont_force:
                return ace_return_to_force(thread, cont, v);
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Return to %d continuations", cont->node);
                return 0;
        }
    }
}

static
int
ace_return_to_app(struct ace_thread *thread, struct gsbc_cont *cont, gsvalue v)
{
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
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Partial applications");
        unlock(&fun->lock);
        return 0;
    } else if (app->numargs > needed_args) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Over-saturated applications");
        unlock(&fun->lock);
        return 0;
    } else {
        int i;
        void *ip;

        switch (cl->code->tag) {
            case gsbc_expr: {
                int needed_args;

                needed_args = cl->code->numfvs + cl->code->numargs - cl->numfvs;

                if (needed_args < app->numargs) {
                    ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Over-saturated applications");
                    unlock(&fun->lock);
                    return 0;
                }
                if (needed_args > app->numargs) {
                    ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Under-saturated applications");
                    unlock(&fun->lock);
                    return 0;
                }

                ip = ace_set_registers_from_closure(thread, cl);
                if (!ip) {
                    ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                    unlock(&fun->lock);
                    return 0;
                }

                for (i = 0; i < app->numargs; i++) {
                    if (thread->nregs >= MAX_NUM_REGISTERS) {
                        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                        unlock(&fun->lock);
                        return 0;
                    }
                    thread->regs[thread->nregs] = app->arguments[i];
                    thread->nregs++;
                }
                thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);
                thread->blocked = 0;
                thread->ip = (struct gsbc *)ip;
                break;
            }
            case gsbc_eprog: {
                struct gsheap_item *hpres;
                struct gsclosure *res;

                hpres = gsreserveheap(sizeof(*res) + (cl->numfvs + app->numargs) * sizeof(gsvalue));
                res = (struct gsclosure *)hpres;

                memset(&hpres->lock, 0, sizeof(hpres->lock));
                hpres->pos = cont->pos;
                hpres->type = gsclosure;
                res->code = cl->code;
                res->numfvs = cl->numfvs + app->numargs;
                for (i = 0; i < cl->numfvs; i++)
                    res->fvs[i] = cl->fvs[i]
                ;
                for (i = 0; i < app->numargs; i++)
                    res->fvs[cl->numfvs + i] = app->arguments[i]
                ;

                unlock(&fun->lock);
                thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);
                return ace_return(thread, cont->pos, (gsvalue)res);
            }
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, fun->pos, "Apply code blocks of type %d", cl->code->tag);
                unlock(&fun->lock);
                return 0;
        }
    }

    unlock(&fun->lock);

    return 1;
}

static void *ace_set_registers_from_bco(struct ace_thread *, struct gsbco *);

static
int
ace_return_to_force(struct ace_thread *thread, struct gsbc_cont *cont, gsvalue v)
{
    struct gsbc_cont_force *force;
    void *ip;
    int i;

    force = (struct gsbc_cont_force *)cont;

    ip = ace_set_registers_from_bco(thread, force->code);
    if (!ip) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return 0;
    }

    for (i = 0; i < force->numfvs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
            return 0;
        }
        thread->regs[thread->nregs++] = force->fvs[i];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return 0;
    }
    thread->regs[thread->nregs++] = v;

    thread->blocked = 0;
    thread->ip = (struct gsbc *)ip;
    thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_force) + force->numfvs * sizeof(gsvalue);

    return 1;
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
    struct gseval *ev;
    int i;

    hp = (struct gsheap_item *)val;
    switch (hp->type) {
        case gsclosure: {
            struct gsclosure *cl;
            struct gsbc *instr;

            cl = (struct gsclosure *)hp;

            switch (cl->code->tag) {
                case gsbc_expr: {
                    thread = ace_thread_alloc();

                    thread->base = val;

                    instr = (struct gsbc *)ace_set_registers_from_closure(thread, cl);

                    if (!instr) {
                        gspoison_unimpl(hp, __FILE__, __LINE__, cl->code->pos, "Too many registers");
                        unlock(&thread->lock);
                        return gstyindir;
                    }

                    thread->blocked = 0;
                    thread->ip = instr;
                    break;
                }
                default:
                    gspoison_unimpl(hp, __FILE__, __LINE__, cl->code->pos, "ace_start_evaluation(%d)", cl->code->tag);
                    return gstyindir;
            }

            break;
        }
        case gsapplication: {
            struct gsapplication *app;
            struct gsbc_cont *cont;
            struct gsbc_cont_app *appcont;

            app = (struct gsapplication *)hp;

            thread = ace_thread_alloc();

            thread->base = val;

            cont = ace_stack_alloc(thread, hp->pos, sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue));
            appcont = (struct gsbc_cont_app *)cont;
            if (!cont) {
                unlock(&thread->lock);
                werrstr("Out of stack space allocating app continuation");
                return gstyeoostack;
            }

            cont->node = gsbc_cont_app;
            cont->pos = hp->pos;
            appcont->numargs = app->numargs;
            for (i = 0; i < app->numargs; i++) {
                appcont->arguments[i] = app->arguments[i];
            }

            thread->blocked = app->fun;
            thread->blockedat = app->hp.pos;

            break;
        }
        default:
            gswerrstr_unimpl(__FILE__, __LINE__, "ace_start_evaluation(type = %d)", hp->type);
            return gstyenosys;
    }

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
ace_set_registers_from_closure(struct ace_thread *thread, struct gsclosure *cl)
{
    void *ip;
    int i;

    ip = ace_set_registers_from_bco(thread, cl->code);
    if (!ip) return 0;
    for (i = 0; i < cl->numfvs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS)
            return 0
        ;
        thread->regs[thread->nregs] = cl->fvs[i];
        thread->nregs++;
    }
    return ip;
}

static
void *
ace_set_registers_from_bco(struct ace_thread *thread, struct gsbco *code)
{
    void *ip;
    int i;

    thread->nregs = 0;
    thread->nsubexprs = 0;

    ip = (uchar*)code + sizeof(*code);
    if ((uintptr)ip % sizeof(struct gsbco *))
        ip = (uchar*)ip + sizeof(struct gsbco *) - (uintptr)ip % sizeof(struct gsbco *)
    ;
    for (i = 0; i < code->numsubexprs; i++) {
        if (thread->nsubexprs >= MAX_NUM_REGISTERS)
            return 0
        ;
        thread->subexprs[thread->nsubexprs] = *(struct gsbco **)ip;
        thread->nsubexprs++;
        ip = (struct gsbco **)ip + 1;
    }
    if ((uintptr)ip % sizeof(gsvalue))
        ip = (uchar*)ip + sizeof(gsvalue) - (uintptr)ip % sizeof(gsvalue)
    ;
    for (i = 0; i < code->numglobals; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS)
            return 0
        ;
        thread->regs[thread->nregs] = *(gsvalue *)ip;
        thread->nregs++;
        ip = (gsvalue*)ip + 1;
    }
    return ip;
}

static Lock ace_thread_lock;
static struct gs_block_class ace_thread_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
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

    memset(&thread->lock, 0, sizeof(thread->lock));
    lock(&thread->lock);

    thread->stackbot = stackbot;
    thread->stacktop = stackbot;
    thread->stacklimit = (uchar*)thread + sizeof(*thread);

    return thread;
}
