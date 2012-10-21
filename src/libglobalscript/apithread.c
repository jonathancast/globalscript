#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "api.h"

static struct api_thread_queue *api_thread_queue;

static void api_take_thread_queue(void);
static void api_release_thread_queue(void);

static struct api_thread *api_add_thread(struct gsrpc_queue *, gsvalue);

static void api_take_thread(struct api_thread *);
static void api_release_thread(struct api_thread *);

static struct gs_block_class api_thread_queue_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "API Thread Queue",
};
static void *api_thread_queue_nursury;

static struct api_thread *api_try_schedule_thread(struct api_thread *);

struct api_thread_pool_args {
    struct gsrpc_queue *rpc_queue;
    gsvalue entry;
};
static void api_thread_pool_main(void *);

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void
apisetupmainthread(struct api_process_rpc_table *table, gsvalue entry)
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

static void api_exec_instr(struct api_thread *, gsvalue);

static
void
api_thread_pool_main(void *arg)
{
    struct api_thread *mainthread, *thread;
    struct api_thread_pool_args *args;

    int i, threadnum;
    int ranthread, hadthread;

    args = (struct api_thread_pool_args *)arg;

    mainthread = api_add_thread(args->rpc_queue, args->entry);

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
                gstypecode st;
                gsvalue instr;

                instr = thread->code->instrs[thread->code->ip].instr;
                st = GS_SLOW_EVALUATE(instr);

                switch (st) {
                    case gstywhnf:
                        api_exec_instr(thread, instr);
                        ranthread = 1;
                        break;
                    case gstystack:
                        break;
                    default:
                        api_abend_unimpl(thread, __FILE__, __LINE__, "API thread advancement (state = %d)", st);
                        break;
                }
                api_release_thread(thread);
            }
        }
        if (!ranthread)
            sleep(1)
        ;
    } while (hadthread);

    ace_down();
}

static
void
api_exec_instr(struct api_thread *thread, gsvalue instr)
{
    struct gs_blockdesc *block;

    instr = gsremove_indirections(instr);

    block = BLOCK_CONTAINING(instr);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)instr;
        api_abend(thread, "%s:%d: undefined", p->file->name, p->lineno);
    } else {
        api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction excecution (%s)", block->class->description);
    }
}

static struct api_code_segment *api_alloc_code_segment(struct api_thread *, gsvalue);

static
struct api_thread *
api_add_thread(struct gsrpc_queue *rpc_queue, gsvalue entry)
{
    int i;
    struct api_thread *thread;

    api_take_thread_queue();

    thread = 0;
    for (i = 0; i < API_NUMTHREADS; i++) {
        api_take_thread(&api_thread_queue->threads[i]);
        if (api_thread_queue->threads[i].active) {
            api_release_thread(&api_thread_queue->threads[i]);
        } else {
            thread = &api_thread_queue->threads[i];
            goto have_thread;
        }
    }
    gsfatal_unimpl(__FILE__, __LINE__, "thread queue overflow");

have_thread:
    api_release_thread_queue();

    thread->active = 1;
    thread->hard = 1;

    thread->process_rpc_queue = rpc_queue;

    thread->code = api_alloc_code_segment(thread, entry);

    return thread;
}

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
    if (api_code_segment_nursury >= END_OF_BLOCK(api_code_segment_nursury_seg))
        api_code_segment_nursury = 0
    ;
    unlock(&api_code_segment_lock);
    return res;
}

static
struct api_thread *
api_try_schedule_thread(struct api_thread *thread)
{
    api_take_thread(thread);
    if (thread->active)
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

static
void
api_take_thread(struct api_thread *thread)
{
    lock(&thread->lock);
}

static
void
api_release_thread(struct api_thread *thread)
{
    unlock(&thread->lock);
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

struct api_abend_rpc {
    struct gsrpc rpc;
    char status[];
};

void
api_abend(struct api_thread *thread, char *msg, ...)
{
    struct gsrpc *rpc;
    struct api_abend_rpc *abend;
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    if (thread->hard) {
        rpc = gsqueue_rpc_alloc(sizeof(*abend) + strlen(argv0) + 2 + strlen(buf) + 1);
        abend = (struct api_abend_rpc *)rpc;
        rpc->tag = api_std_rpc_abend;
        sprint(abend->status, "%s: %s", argv0, buf);
        gsqueue_send_rpc(thread->process_rpc_queue, rpc);
    }

    thread->active = 0;
    gswarning("%s:%d: api_abend: storing thread exit status: deferred", __FILE__, __LINE__);
}

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
