#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsregtables.h"
#include "gsheap.h"
#include "api.h"

/* §section Declarations */

static struct api_thread_queue *api_thread_queue;

static void api_take_thread_queue(void);
static void api_release_thread_queue(void);

static struct api_thread *api_add_thread(struct gsrpc_queue *, struct api_thread_table *api_thread_table, void *, struct api_prim_table *, gsvalue);

static struct gs_block_class api_thread_queue_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "API Thread Queue",
};
static void *api_thread_queue_nursury;

static struct api_thread *api_try_schedule_thread(struct api_thread *);

struct api_thread_pool_args {
    struct gsrpc_queue *rpc_queue;
    struct api_thread_table *api_thread_table;
    void *api_main_thread_data;
    struct api_prim_table *api_prim_table;
    gsvalue entry;
};
static void api_thread_pool_main(void *);

/* §section Setup */

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void
apisetupmainthread(struct api_process_rpc_table *table, struct api_thread_table *api_thread_table, void *api_main_thread_data, struct api_prim_table *api_prim_table, gsvalue entry)
{
    struct gsrpc *rpc;
    struct gsrpc_queue *rpc_queue;
    struct api_thread_pool_args api_thread_pool_args;
    int api_pool;

    if (api_thread_queue)
        gsfatal("apisetupmainthread called twice")
    ;

    api_thread_queue = gs_sys_seg_suballoc(&api_thread_queue_descr, &api_thread_queue_nursury, sizeof(*api_thread_queue), sizeof(void*));
    memset(api_thread_queue, 0, sizeof(*api_thread_queue));

    rpc_queue = gsqueue_alloc();

    api_thread_pool_args.rpc_queue = rpc_queue;
    api_thread_pool_args.api_thread_table = api_thread_table;
    api_thread_pool_args.api_main_thread_data = api_main_thread_data;
    api_thread_pool_args.api_prim_table = api_prim_table;
    api_thread_pool_args.entry = entry;
    if ((api_pool = gscreate_thread_pool(api_thread_pool_main, &api_thread_pool_args, sizeof(api_thread_pool_args))) < 0)
        gsfatal("Couldn't create API thread pool: %r")
    ;

    /* OK, this is tricky

        We need to write this code in a very specific order:
        §begin{itemize}
            §item First, set up the RPC queue for the main process.
            §item Then, have the main process actually loop over the RPC queue and handle the messages delivered.
                This will require taking a dispatch table.
            §item When creating the API pool initially, have it find the main thread immediately and immediately send a failure message to the main process.
            §item Then we can develop the API pool and have it api_abort by sending failure messages to the main process.
        §end{itemize}
    */
    while (rpc = gsqueue_get_rpc(rpc_queue)) {
        int tag;

        tag = rpc->tag;
        if (tag >= table->numrpcs) {
            unlock(&rpc->lock);
            gsfatal("%s:%d: apisetupmainthread: Panic: Unknown RPC %d in process of type %s", __FILE__, __LINE__, tag, table->name);
        } else {
            (*table->handlers[tag])(rpc);
        }
    }
    gsfatal("%s:%d: Panic: apisetupmainthread: ran out of RPCs somehow", __FILE__, __LINE__);
}

/* §section Main loop */

static void api_exec_instr(struct api_thread *, gsvalue);

static void api_send_done_rpc(struct api_thread *);
static void api_send_abend_rpc(struct api_thread *, char *, ...);

#define API_TERMINATION_QUEUE_LENGTH 0x100
static Lock api_at_termination_queue_lock;
static int api_at_termination_queue_length;
static api_termination_callback *api_at_termination_queue[API_TERMINATION_QUEUE_LENGTH];

static
void
api_thread_pool_main(void *arg)
{
    struct api_thread *mainthread, *thread;
    struct api_thread_pool_args *args;

    int threadnum;
    int ranthread, hadthread;

    args = (struct api_thread_pool_args *)arg;

    mainthread = api_add_thread(args->rpc_queue, args->api_thread_table, args->api_main_thread_data, args->api_prim_table, args->entry);

    api_release_thread(mainthread);

    do {
        hadthread = ranthread = 0;
        for (threadnum = 0; threadnum < API_NUMTHREADS; threadnum++) {
            thread = 0;
            api_take_thread_queue();
            for (; threadnum < API_NUMTHREADS && !thread; threadnum++) {
                thread = api_try_schedule_thread(&api_thread_queue->threads[threadnum]);
            }
            api_release_thread_queue();
            if (thread) {
                hadthread = 1;

                switch (thread->state) {
                    case api_thread_st_active: {
                        gstypecode st;
                        gsvalue instr;
                        struct api_code_segment *code;

                        code = thread->code;

                        instr = code->instrs[code->ip].instr;
                        st = GS_SLOW_EVALUATE(instr);

                        switch (st) {
                            case gstywhnf:
                                api_exec_instr(thread, instr);
                                ranthread = 1;
                                break;
                            case gstystack:
                                break;
                            case gstyindir:
                                code->instrs[code->ip].instr = gsremove_indirections(instr);
                                ranthread = 1;
                                break;
                            case gstyenosys:
                                api_abend(thread, "Un-implemented operation: %r");
                                break;
                            default:
                                api_abend_unimpl(thread, __FILE__, __LINE__, "API thread advancement (state = %d)", st);
                                break;
                        }
                        break;
                    }
                    case api_thread_st_terminating_on_done:
                    case api_thread_st_terminating_on_abend: {
                        enum api_prim_execution_state st;

                        st = thread->api_thread_table->thread_term_status(thread);
                        switch (st) {
                            case api_st_success:
                                if (thread->hard) {
                                    if (thread->state == api_thread_st_terminating_on_done) {
                                        api_send_done_rpc(thread);
                                    } else {
                                        api_send_abend_rpc(thread, "%s", thread->status);
                                    }
                                }
                                thread->state = api_thread_st_unused;
                                break;
                            case api_st_blocked:
                                break;
                            default:
                                if (thread->hard)
                                    api_send_abend_rpc(thread, "%s:%d: Handle state %d from thread terminator next", __FILE__, __LINE__, st)
                                ;
                                thread->state = api_thread_st_unused;
                                break;
                        }
                        break;
                    }
                    default: {
                        if (thread->hard)
                            api_send_abend_rpc(thread, "%s:%d: Handle thread state %d next", __FILE__, __LINE__, thread->state)
                        ;
                        thread->state = api_thread_st_unused;
                        break;
                    }
                }
                api_release_thread(thread);
            }
        }
        if (!ranthread)
            if (sleep(1) < 0)
                gswarning("%s:%d: sleep returned a negative number", __FILE__, __LINE__)
        ;
    } while (hadthread);

    lock(&api_at_termination_queue_lock);
    while (api_at_termination_queue_length--) {
        api_at_termination_queue[api_at_termination_queue_length]();
    }
    unlock(&api_at_termination_queue_lock);

    ace_down();
}

void
api_at_termination(api_termination_callback *cb)
{
    lock(&api_at_termination_queue_lock);
    if (api_at_termination_queue_length >= API_TERMINATION_QUEUE_LENGTH)
        gsfatal_unimpl(__FILE__, __LINE__, "API Termination Queue overflow")
    ;
    api_at_termination_queue[api_at_termination_queue_length++] = cb;
    unlock(&api_at_termination_queue_lock);
}

/* §section Execution */

static void api_unpack_block_statement(struct api_thread *, struct gsclosure *);

static
void
api_exec_instr(struct api_thread *thread, gsvalue instr)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(instr);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)instr;
        switch (p->type) {
            case gserror_undefined:
                api_abend(thread, "%P: undefined", p->pos);
                break;
            case gserror_generated:
                api_abend(thread, "%P: %s", p->pos, p->message);
                break;
            default:
                api_abend(thread, "%P: unknown error type %d", p->pos, p->type);
                break;
        }
    } else if (gsisimplementation_failure_block(block)) {
        struct gsimplementation_failure *p;
        char buf[0x100];

        p = (struct gsimplementation_failure *)instr;
        gsimplementation_failure_format(buf, buf + sizeof(buf), p);
        api_abend(thread, "%s", buf);
    } else if (gsisheap_block(block)) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)instr;
        switch (hp->type) {
            case gsclosure: {
                struct gsclosure *cl;

                cl = (struct gsclosure *)hp;
                switch (cl->code->tag) {
                    case gsbc_eprog:
                        api_unpack_block_statement(thread, cl);
                        return;
                    default:
                        api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction execution (%d closures)", cl->code->tag);
                        return;
                }
            }
            default:
                api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction execution (%d exprs)", hp->type);
                return;
        }
    } else if (gsiseprim_block(block)) {
        struct gseprim *eprim;
        struct api_prim_table *table;

        eprim = (struct gseprim *)instr;
        table = thread->api_prim_table;
        if (eprim->index < 0) {
            api_abend(thread, "%P: Unknown primitive", eprim->pos);
        } else if (eprim->index >= table->numprims) {
            api_abend(thread, "%P: Primitive out of bounds", eprim->pos);
        } else {
            enum api_prim_execution_state st;

            st = table->execs[eprim->index](thread, eprim);
            switch (st) {
                case api_st_success:
                    thread->code->ip++;
                    if (thread->code->ip >= thread->code->size)
                        api_done(thread)
                    ;
                    break;
                case api_st_error:
                    /* We assume the exec function called api_abend */
                    break;
                default:
                    api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction execution with state %d", st);
                    break;
            }
        }
    } else {
        api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction execution (%s)", block->class->description);
    }
}

static struct api_promise *api_alloc_promise(void);

static
void
api_unpack_block_statement(struct api_thread *thread, struct gsclosure *cl)
{
    void *pin;
    struct gsbco *code;
    struct gsbco **psubexpr;
    struct gsbc *pinstr;
    struct gsbco *subexprs[MAX_NUM_REGISTERS];

    int nregs;
    gsvalue regs[MAX_NUM_REGISTERS];

    int nstatements;
    gsvalue rhss[MAX_NUM_REGISTERS];
    struct api_promise *lhss[MAX_NUM_REGISTERS];
    int i;

    code = cl->code;
    pin = (uchar*)code + sizeof(*code);

    nregs = 0;

    for (i = 0; i < code->numsubexprs; i++) {
        psubexpr = (struct gsbco **)pin;
        subexprs[i] = *psubexpr++;
        pin = psubexpr;
    }

    for (i = 0; i < code->numglobals; i++) {
        api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: get global variable");
        return;
    }

    for (i = 0; i < code->numfvs; i++) {
        api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: get free variable");
        return;
    }

    for (i = 0; i < code->numargs; i++) {
        if (nregs >= MAX_NUM_REGISTERS) {
            api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: too many registers (max 0x%x)", MAX_NUM_REGISTERS);
            return;
        }
        regs[nregs] = cl->fvs[code->numfvs + i];
        nregs++;
    }

    nstatements = 0;
    for (;;) {
        pinstr = (struct gsbc *)pin;
        switch (pinstr->instr) {
            case gsbc_op_alloc: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                if (nregs > MAX_NUM_REGISTERS) {
                    api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: too many registers (max 0x%x)", MAX_NUM_REGISTERS);
                    return;
                }

                subexpr = subexprs[pinstr->args[0]];

                cl = gsreserveheap(sizeof(*cl) + pinstr->args[1] * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->code = subexpr;
                cl->numfvs = pinstr->args[1];
                for (i = 0; i < pinstr->args[1]; i++) {
                    api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: free variables of .alloc");
                    return;
                }
                regs[nregs] = (gsvalue)cl;
                nregs++;

                pin = GS_NEXT_BYTECODE(pinstr, 2 + pinstr->args[1]);
                continue;
            }
            case gsbc_op_bind: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                if (nregs > MAX_NUM_REGISTERS) {
                    api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: too many registers (max 0x%x)", MAX_NUM_REGISTERS);
                    return;
                }

                subexpr = subexprs[pinstr->args[0]];

                cl = gsreserveheap(sizeof(*cl) + pinstr->args[1] * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->code = subexpr;
                cl->numfvs = pinstr->args[1];
                for (i = 0; i < pinstr->args[1]; i++) {
                    api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: free variables of .bind");
                    return;
                }
                rhss[nstatements] = (gsvalue)cl;
                lhss[nstatements] = api_alloc_promise();
                regs[nregs] = (gsvalue)lhss[nstatements];

                nstatements++;
                nregs++;

                pin = GS_NEXT_BYTECODE(pinstr, 2 + pinstr->args[1]);
                continue;
            }
            case gsbc_op_body: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                if (nstatements > MAX_NUM_REGISTERS) {
                    api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: too many statements (max 0x%x)", MAX_NUM_REGISTERS);
                    return;
                }

                subexpr = subexprs[pinstr->args[0]];

                cl = gsreserveheap(sizeof(*cl) + pinstr->args[1] * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->code = subexpr;
                cl->numfvs = pinstr->args[1];
                for (i = 0; i < pinstr->args[1]; i++) {
                    cl->fvs[i] = regs[pinstr->args[2 + i]];
                }
                rhss[nstatements] = (gsvalue)cl;
                nstatements++;
                nregs++;
                goto got_statements;
            }
            default:
                api_abend_unimpl(thread, __FILE__, __LINE__, "api_unpack_block_statement: %d opcodes", pinstr->instr);
                return;
        }
        pinstr = GS_NEXT_BYTECODE(pinstr, 2 + pinstr->args[1]);
        pin = pinstr;
    }

got_statements:
    nstatements--;
    thread->code->instrs[thread->code->ip].instr = rhss[nstatements];
    while (nstatements--) {
        if (thread->code->ip <= 0) {
            api_abend_unimpl(thread, __FILE__, __LINE__, "code segment overflow");
            return;
        }
        thread->code->ip--;
        thread->code->instrs[thread->code->ip].instr = rhss[nstatements];
        thread->code->instrs[thread->code->ip].presult = lhss[nstatements];
    }
}

/* §section Adding threads */

static struct api_code_segment *api_alloc_code_segment(struct api_thread *, gsvalue);

static
struct api_thread *
api_add_thread(struct gsrpc_queue *rpc_queue, struct api_thread_table *api_thread_table, void *main_thread_data, struct api_prim_table *api_prim_table, gsvalue entry)
{
    int i;
    struct api_thread *thread;

    api_take_thread_queue();

    thread = 0;
    for (i = 0; i < API_NUMTHREADS; i++) {
        api_take_thread(&api_thread_queue->threads[i]);
        if (api_thread_queue->threads[i].state == api_thread_st_unused) {
            thread = &api_thread_queue->threads[i];
            goto have_thread;
        } else {
            api_release_thread(&api_thread_queue->threads[i]);
        }
    }
    gsfatal_unimpl(__FILE__, __LINE__, "thread queue overflow");

have_thread:
    api_release_thread_queue();

    thread->state = api_thread_st_active;
    thread->hard = 1;

    thread->process_rpc_queue = rpc_queue;
    thread->api_thread_table = api_thread_table;
    thread->api_prim_table = api_prim_table;
    thread->client_data = main_thread_data;
    thread->status = 0;

    thread->code = api_alloc_code_segment(thread, entry);

    return thread;
}

/* §section Allocation */

#define API_CODE_SEGMENT_BLOCK_SIZE 0x400
static Lock api_code_segment_lock;
static void *api_code_segment_nursury;
static struct gs_block_class api_code_segment_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "API code segments",
};

static
struct api_code_segment *
api_alloc_code_segment(struct api_thread *thread, gsvalue entry)
{
    struct api_code_segment *res;
    struct gs_blockdesc *api_code_segment_nursury_seg;
    struct api_instr *start_of_instrs, *end_of_instrs;

    lock(&api_code_segment_lock);
    if (!api_code_segment_nursury) {
        api_code_segment_nursury_seg = gs_sys_seg_alloc(&api_code_segment_descr);
        api_code_segment_nursury = (uchar*)api_code_segment_nursury_seg + sizeof(*api_code_segment_nursury_seg);
    } else {
        api_code_segment_nursury_seg = BLOCK_CONTAINING(api_code_segment_nursury);
    }
    res = api_code_segment_nursury;
    api_code_segment_nursury =
        (uchar*)api_code_segment_nursury
        - (uintptr)api_code_segment_nursury % API_CODE_SEGMENT_BLOCK_SIZE
        + API_CODE_SEGMENT_BLOCK_SIZE
    ;
    start_of_instrs = &res->instrs[0];
    end_of_instrs = api_code_segment_nursury;
    res->size = end_of_instrs - start_of_instrs;
    res->ip = res->size - 1;
    res->instrs[res->ip].instr = entry;
    res->instrs[res->ip].presult = api_alloc_promise();
    if (api_code_segment_nursury >= END_OF_BLOCK(api_code_segment_nursury_seg))
        api_code_segment_nursury = 0
    ;
    unlock(&api_code_segment_lock);
    return res;
}

static Lock api_promise_segment_lock;
static void *api_promise_segment_nursury;
static struct gs_block_class api_promise_segment_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "API promises",
};

static
struct api_promise *
api_alloc_promise()
{
    struct api_promise *res;

    lock(&api_promise_segment_lock);
    res = gs_sys_seg_suballoc(&api_promise_segment_descr, &api_promise_segment_nursury, sizeof(*res), sizeof(void*));
    unlock(&api_promise_segment_lock);

    memset(res, 0, sizeof(*res));

    return res;
}

/* §section Helper Functions */

static
struct api_thread *
api_try_schedule_thread(struct api_thread *thread)
{
    api_take_thread(thread);
    if (
        thread->state == api_thread_st_active
        || thread->state == api_thread_st_terminating_on_done
        || thread->state == api_thread_st_terminating_on_abend
    )
        return thread
    ; else {
        api_release_thread(thread);
        return 0;
    }
}

static
void
api_take_thread_queue()
{
    lock(&api_thread_queue->lock);
}

static
void
api_release_thread_queue()
{
    unlock(&api_thread_queue->lock);
}

void
api_take_thread(struct api_thread *thread)
{
    lock(&thread->lock);
}

void
api_release_thread(struct api_thread *thread)
{
    unlock(&thread->lock);
}

void *
api_thread_client_data(struct api_thread *thread)
{
    return thread->client_data;
}

/* §section Terminate threads */

struct api_done_rpc {
    struct gsrpc rpc;
};

void
api_done(struct api_thread *thread)
{
    thread->state = api_thread_st_terminating_on_done;
    thread->status = "";
}

static
void
api_send_done_rpc(struct api_thread *thread)
{
    struct gsrpc *rpc;
    struct api_done_rpc *done;

    rpc = gsqueue_rpc_alloc(sizeof(*done));
    done = (struct api_done_rpc *)rpc;
    rpc->tag = api_std_rpc_done;
    gsqueue_send_rpc(thread->process_rpc_queue, rpc);
}

struct api_abend_rpc {
    struct gsrpc rpc;
    char status[];
};

static struct gs_block_class api_thread_status_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "API Thread Status",
};
static void *api_thread_status_nursury;
static Lock api_thread_status_lock;

void
api_abend(struct api_thread *thread, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    thread->state = api_thread_st_terminating_on_abend;
    lock(&api_thread_status_lock);
    thread->status = gs_sys_seg_suballoc(&api_thread_status_descr, &api_thread_status_nursury, strlen(buf) + 1, sizeof(char));
    unlock(&api_thread_status_lock);
    strcpy(thread->status, buf);
}

static
void
api_send_abend_rpc(struct api_thread *thread, char *msg, ...)
{
    struct gsrpc *rpc;
    struct api_abend_rpc *abend;
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    rpc = gsqueue_rpc_alloc(sizeof(*abend) + strlen(argv0) + 2 + strlen(buf) + 1);
    abend = (struct api_abend_rpc *)rpc;
    rpc->tag = api_std_rpc_abend;
    sprint(abend->status, "%s: %s", argv0, buf);
    gsqueue_send_rpc(thread->process_rpc_queue, rpc);
}

void
api_abend_unimpl(struct api_thread *thread, char *srcfile, int lineno, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    api_abend(thread, "%s:%d: %s next", srcfile, lineno, buf);
}

/* §section RPC handlers for main process */

void
api_main_process_unimpl_rpc(struct gsrpc *rpc)
{
    int tag;

    tag = rpc->tag;
    rpc->status = gsrpc_failed;
    rpc->err = "unimpl";
    unlock(&rpc->lock);
    gsfatal("Panic: unimplemented rpc %d in Unix pool", tag);
}

void
api_main_process_handle_rpc_done(struct gsrpc *rpc)
{
    struct api_done_rpc *done;

    done = (struct api_done_rpc *)rpc;

    rpc->status = gsrpc_succeeded;
    unlock(&rpc->lock);

    exits("");
}

void
api_main_process_handle_rpc_abend(struct gsrpc *rpc)
{
    char *status;
    struct api_abend_rpc *abend;

    abend = (struct api_abend_rpc *)rpc;

    status = abend->status;

    rpc->status = gsrpc_succeeded;
    unlock(&rpc->lock);

    fprint(2, "%s\n", status);
    exits(status);
}

/* §section API primitive implementations */

enum
api_prim_execution_state
api_thread_handle_prim_unit(struct api_thread *thread, struct gseprim *eprim)
{
    struct api_code_segment *code;
    struct api_promise *pres;

    code = thread->code;
    pres = code->instrs[code->ip].presult;
    lock(&pres->lock);
    pres->value = eprim->arguments[1];
    unlock(&pres->lock);
    return api_st_success;
}
