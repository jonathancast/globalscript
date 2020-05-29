/* §source.file API */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "gsbytecode.h"
#include "api.h"
#include "gsproc.h"

/* §section Declarations */

static struct api_thread_queue *api_thread_queue;

static void api_take_thread_queue(void);
static void api_release_thread_queue(void);

static struct api_thread *api_add_thread(struct gspos, struct api_thread_table *api_thread_table, void *, struct api_prim_table *, gsvalue);

static struct gs_sys_global_block_suballoc_info api_thread_queue_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "API Thread Queue",
    },
};

/* §section Main loop */

static int api_exec_instr(struct api_thread *, gsvalue);
static void api_exec_err(struct api_thread *, gsvalue, gstypecode);

#define API_TERMINATION_QUEUE_LENGTH 0x100
static Lock api_at_termination_queue_lock;
static int api_at_termination_queue_length;
static api_termination_callback *api_at_termination_queue[API_TERMINATION_QUEUE_LENGTH];

static int api_gc_trace_thread_queue(struct gsstringbuilder *);

static void api_handle_gc_failed(struct gsstringbuilder *);

struct api_thread_stats {
    vlong thread_lifetime;
    vlong loops, instrs, loops_waiting;
};

static void api_thread_pool_shutdown(struct api_thread_stats *);

/* Note: §c{apisetupmainthread} §emph{never returns; it calls §c{exits} */
void
apisetupmainthread(struct gspos pos, struct api_thread_table *api_main_thread_table, void *api_main_thread_data, struct api_prim_table *api_prim_table, gsvalue entry)
{
    struct api_thread *mainthread, *thread;

    int threadnum;
    int suspended_runnable_thread;
    struct api_thread_stats stats;

    if (api_thread_queue)
        gsfatal("apisetupmainthread called twice")
    ;

    api_thread_queue = gs_sys_global_block_suballoc(&api_thread_queue_info, sizeof(*api_thread_queue));
    memset(api_thread_queue, 0, sizeof(*api_thread_queue));

    mainthread = api_add_thread(pos, api_main_thread_table, api_main_thread_data, api_prim_table, entry);
    mainthread->ismain = 1;

    api_release_thread(mainthread);

    mainthread = 0;

    stats.thread_lifetime = 0;
    stats.loops = stats.instrs = stats.loops_waiting = 0;

    for (;;) {
        suspended_runnable_thread = 0;

        if (gs_sys_should_gc()) {
            struct gsstringbuilder *err;

            gsstatprint("Before garbage collection: %dMB used\n", gs_sys_memory_allocated_size() / 0x400 / 0x400);
            err = gsreserve_string_builder();

            gs_sys_wait_for_gc();
            if (gs_sys_start_gc(err) < 0) {
                gsfinish_string_builder(err);
                api_handle_gc_failed(err);
                goto gc_done;
            }

            err = gsreserve_string_builder();
            if (api_gc_trace_thread_queue(err) < 0) {
                gsfinish_string_builder(err);
                api_handle_gc_failed(err);
                goto gc_done;
            }

            if (gs_sys_finish_gc(err) < 0) {
                gsfinish_string_builder(err);
                api_handle_gc_failed(err);
                goto gc_done;
            }

            gsstatprint("After garbage collection: %dMB used\n", gs_sys_memory_allocated_size() / 0x400 / 0x400);
        }
    gc_done:

        if (gs_sys_memory_exhausted()) {
            gswarning("%s:%d: About to terminate on out of memory (%dMB used)", __FILE__, __LINE__, gs_sys_memory_allocated_size() / 0x400 / 0x400);
            for (threadnum = 0; threadnum < API_NUMTHREADS; threadnum++) {
                api_take_thread_queue();
                thread = &api_thread_queue->threads[threadnum];
                api_release_thread_queue();
                api_take_thread(thread);
                if (thread->state == api_thread_st_active)
                    api_abend(thread, UNIMPL("Terminate on out of memory"))
                ;
                api_release_thread(thread);
            }
        }

        for (threadnum = 0; threadnum < API_NUMTHREADS; threadnum++) {
            api_take_thread_queue();
            thread = &api_thread_queue->threads[threadnum];
            api_release_thread_queue();

            api_take_thread(thread);
            switch (thread->state) {
                case api_thread_st_unused:
                case api_thread_st_running: {
                    api_release_thread(thread);
                    break;
                }
                case api_thread_st_active: {
                    gstypecode st;
                    struct gspos pos;
                    gsvalue instr;

                    /* Don't run arbitrary code while holding a lock!
                       §emph{But} we want to prevent this thread from being scheduled by another CPU while it's unlocked
                    */
                    thread->state = api_thread_st_running;
                    api_release_thread(thread);

                    stats.loops++;

                    api_take_thread(thread);

                    pos = thread->code->instrs[thread->code->ip].pos;
                    instr = thread->code->instrs[thread->code->ip].instr;
                    api_release_thread(thread);

                    st = GS_SLOW_EVALUATE(pos, instr);

                    switch (st) {
                        case gstywhnf:
                            stats.instrs++;

                            if (api_exec_instr(thread, instr) > 0)
                                suspended_runnable_thread = 1
                            ;
                            break;
                        case gstyerr:
                        case gstyimplerr:
                            api_take_thread(thread);
                            api_exec_err(thread, instr, st);
                            api_release_thread(thread);
                            break;
                        case gstystack:
                            stats.loops_waiting++;
                            api_take_thread(thread);
                            thread->state = api_thread_st_active;
                            api_release_thread(thread);
                            break;
                        case gstyindir:
                            api_take_thread(thread);
                            thread->code->instrs[thread->code->ip].instr = GS_REMOVE_INDIRECTION(pos, instr);
                            suspended_runnable_thread = 1;
                            thread->state = api_thread_st_active;
                            api_release_thread(thread);
                            break;
                        case gstyenosys:
                            api_take_thread(thread);
                            api_abend(thread, "Un-implemented operation: %r");
                            api_release_thread(thread);
                            break;
                        default:
                            api_take_thread(thread);
                            api_abend_unimpl(thread, __FILE__, __LINE__, "API thread advancement (state = %d)", st);
                            api_release_thread(thread);
                            break;
                    }
                    break;
                }
                case api_thread_st_terminating_on_done:
                case api_thread_st_terminating_on_abend: {
                    enum api_prim_execution_state st;
                    int thread_abended;

                    thread_abended = thread->state == api_thread_st_terminating_on_abend;

                    stats.loops++;

                    if (gsflag_stat_collection && !thread->prog_term_time) {
                        thread->prog_term_time = nsec();
                        stats.thread_lifetime += thread->prog_term_time - thread->start_time;
                    }
                    st = thread->api_thread_table->thread_term_status(thread);
                    switch (st) {
                        case api_st_success: {
                            int have_other_threads;

                            api_take_thread_queue();
                            api_thread_queue->numthreads--;
                            have_other_threads = api_thread_queue->numthreads > 0;
                            api_release_thread_queue();
                            thread->state = api_thread_st_unused;

                            if (thread->ismain) {
                                if (have_other_threads) {
                                    api_thread_pool_shutdown(&stats);
                                    gs_sys_num_procs--;
                                    gsfatal(UNIMPL("Thread is main thread and there are background threads --- fork into background.  Do not release ACE or run shutdown hooks yet."));
                                } else {
                                    api_thread_pool_shutdown(&stats);
                                    if (thread_abended) {
                                        fprint(2, "%s\n", thread->status->start);
                                        gs_sys_num_procs--;
                                        exits(thread->status->start);
                                    } else {
                                        gs_sys_num_procs--;
                                        exits("");
                                    }
                                }
                            } else { /* Thread is background thread */
                                if (have_other_threads) {
                                    api_thread_pool_shutdown(&stats);
                                    gs_sys_num_procs--;
                                    gsfatal(UNIMPL("Thread is background thread and there are other threads --- shut down thread and keep going.  Do not release ACE or run shutdown hooks yet."));
                                } else {
                                    api_thread_pool_shutdown(&stats);
                                    gs_sys_num_procs--;
                                    gsfatal(UNIMPL("Thread is last background thread --- shut down.  Always exits(\"\") in this case; exit status doesn't matter anyway. Complication: Need to run the stuff at the bottom of this thread, too."));
                                }
                            }
                            break;
                        }
                        case api_st_blocked:
                            break;
                        default:
                            thread->state = api_thread_st_unused;
                            api_thread_pool_shutdown(&stats);
                            gs_sys_num_procs--;
                            gsfatal(UNIMPL("Handle state %d from thread terminator next"), st);
                            break;
                    }
                    api_release_thread(thread);
                    break;
                }
                default: {
                    thread->state = api_thread_st_unused;
                    api_thread_pool_shutdown(&stats);
                    gs_sys_num_procs--;
                    gsfatal(UNIMPL("Handle thread state %d next"), thread->state);
                    api_release_thread(thread);
                    break;
                }
            }
        }
        if (!suspended_runnable_thread)
            if (sleep(1) < 0)
                gswarning("%s:%d: sleep returned a negative number", __FILE__, __LINE__)
        ;
    }
}

void
api_thread_pool_shutdown(struct api_thread_stats *stats)
{
    lock(&api_at_termination_queue_lock);
    while (api_at_termination_queue_length--) {
        api_at_termination_queue[api_at_termination_queue_length]();
    }
    unlock(&api_at_termination_queue_lock);

    if (gsflag_stat_collection) {
        gsstatprint("# API threads: %d\n", api_thread_queue->numthreads);
        gsstatprint("API thread total lifetime: %llds %lldms\n", stats->thread_lifetime / 1000 / 1000 / 1000, (stats->thread_lifetime / 1000 / 1000) % 1000);
        if (stats->loops) gsstatprint("# API thread iterations: %lld (%0.2g%% instructions, %0.2g%% waiting)\n", stats->loops, ((double)stats->instrs / stats->loops) * 100, ((double)stats->loops_waiting / stats->loops) * 100);
    }

    ace_down();
}

void
api_handle_gc_failed(struct gsstringbuilder *err)
{
    int i;
    struct api_thread *thread;

    gsfinish_string_builder(err);

    for (i = 0; i < API_NUMTHREADS; i++) {
        thread = &api_thread_queue->threads[i];
        if (thread->client_data) thread->api_thread_table->gc_failure_cleanup(&thread->client_data);
        if (thread->api_prim_blocking) {
            if (thread->api_prim_blocking->forward) thread->api_prim_blocking = thread->api_prim_blocking->forward;
            thread->api_prim_blocking->gccleanup(thread->api_prim_blocking);
        }
    }

    for (i = 0; i < API_NUMTHREADS; i++) {
        api_take_thread_queue();
        thread = &api_thread_queue->threads[i];
        api_release_thread_queue();
        api_take_thread(thread);
        if (thread->state == api_thread_st_active) api_abend(thread, "%s", err->start);
        api_release_thread(thread);
    }

    gs_sys_gc_failed(err->start);
}

static int api_gc_copy_thread(struct gsstringbuilder *, struct api_thread *, struct api_thread *);
static int api_gc_evacuate_thread(struct gsstringbuilder *, struct api_thread *);

static
int
api_gc_trace_thread_queue(struct gsstringbuilder *err)
{
    struct api_thread_queue *newqueue;
    int i;

    newqueue = gs_sys_global_block_suballoc(&api_thread_queue_info, sizeof(*api_thread_queue));
    memset(&newqueue->lock, 0, sizeof(newqueue->lock));
    newqueue->numthreads = api_thread_queue->numthreads;

    for (i = 0; i < API_NUMTHREADS; i++)
        if (api_gc_copy_thread(err, &newqueue->threads[i], &api_thread_queue->threads[i]) < 0)
            return -1
    ;

    api_thread_queue = newqueue;

    for (i = 0; i < API_NUMTHREADS; i++)
        if (api_gc_evacuate_thread(err, &api_thread_queue->threads[i]) < 0)
            return -1
    ;

    return 0;
}

static
int
api_gc_copy_thread(struct gsstringbuilder *err, struct api_thread *dest_thread, struct api_thread *src_thread)
{
    memset(&dest_thread->lock, 0, sizeof(dest_thread->lock));
    dest_thread->ismain = src_thread->ismain;
    dest_thread->start_time = src_thread->start_time;
    dest_thread->prog_term_time = src_thread->prog_term_time;
    dest_thread->state = src_thread->state;
    dest_thread->api_thread_table = src_thread->api_thread_table;
    dest_thread->api_prim_table = src_thread->api_prim_table;
    dest_thread->client_data = src_thread->client_data;
    dest_thread->status = src_thread->status;
    dest_thread->code = src_thread->code;
    dest_thread->api_prim_blocking = src_thread->api_prim_blocking;
    dest_thread->forward = 0;

    src_thread->forward = dest_thread;

    return 0;
}

static int api_gc_trace_code_segment(struct gsstringbuilder *, struct api_code_segment **);

static
int
api_gc_evacuate_thread(struct gsstringbuilder *err, struct api_thread *thread)
{
    gsvalue gcv, gctemp;

    gcv = (gsvalue)thread->client_data;
    if (gcv && GS_GC_TRACE(err, &gcv) < 0) return -1;
    thread->client_data = (void*)gcv;

    if (thread->status && gs_sys_block_in_gc_from_space(thread->status) && gsstring_builder_trace(err, &thread->status) < 0) return -1;

    if (thread->code && api_gc_trace_code_segment(err, &thread->code) < 0) return -1;

    if (thread->api_prim_blocking && gs_sys_block_in_gc_from_space(thread->api_prim_blocking)) {
        if (thread->api_prim_blocking->forward) {
            thread->api_prim_blocking = thread->api_prim_blocking->forward;
        } else {
            struct api_prim_blocking *newblocking;

            if (!(newblocking = thread->api_prim_blocking->gccopy(err, thread->api_prim_blocking))) return -1;
            thread->api_prim_blocking = thread->api_prim_blocking->forward = newblocking;
            if (thread->api_prim_blocking->gcevacuate(err, newblocking) < 0) return -1;
        }
    }

    return 0;
}

void
api_at_termination(api_termination_callback *cb)
{
    lock(&api_at_termination_queue_lock);
    if (api_at_termination_queue_length >= API_TERMINATION_QUEUE_LENGTH)
        gsfatal(UNIMPL("API Termination Queue overflow"))
    ;
    api_at_termination_queue[api_at_termination_queue_length++] = cb;
    unlock(&api_at_termination_queue_lock);
}

/* Move before §ccode{} §emph{only once the GC patch is done!} */
struct api_thread *
api_thread_gc_forward(struct api_thread *thread)
{
    return thread->forward ? thread->forward : thread;
}

/* §section Execution */

static void api_unpack_block_statement(struct api_thread *, struct gsclosure *);

static void api_update_promise(struct api_promise *, gsvalue);

static
int
api_exec_instr(struct api_thread *thread, gsvalue instr)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(instr);

    if (gsisimplementation_failure_block(block)) {
        struct gsimplementation_failure *p;
        char buf[0x100];

        p = (struct gsimplementation_failure *)instr;
        gsimplementation_failure_format(buf, buf + sizeof(buf), p);
        api_take_thread(thread);
        api_abend(thread, "%s", buf);
        api_release_thread(thread);
        return 0;
    } else if (gsisheap_block(block)) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)instr;
        switch (hp->type) {
            case gsclosure: {
                struct gsclosure *cl;

                cl = (struct gsclosure *)hp;
                switch (cl->cl.code->tag) {
                    case gsbc_impprog:
                        api_take_thread(thread);
                        api_unpack_block_statement(thread, cl);
                        thread->state = api_thread_st_active;
                        api_release_thread(thread);
                        return 1;
                    default:
                        api_take_thread(thread);
                        api_abend(thread, UNIMPL("API instruction execution (%d closures)"), cl->cl.code->tag);
                        api_release_thread(thread);
                        return 0;
                }
            }
            default:
                api_take_thread(thread);
                api_abend(thread, UNIMPL("API instruction execution (%d exprs)"), hp->type);
                api_release_thread(thread);
                return 0;
        }
    } else if (gsisapiprim_block(block)) {
        struct gsapiprim *apiprim;
        struct api_prim_table *table;

        apiprim = (struct gsapiprim *)instr;
        api_take_thread(thread);
        table = thread->api_prim_table;
        if (apiprim->p.index < 0) {
            api_abend(thread, "%P: Unknown primitive", apiprim->pos);
            api_release_thread(thread);
            return 0;
        } else if (apiprim->p.index >= table->numprims) {
            api_abend(thread, "%P: Primitive out of bounds", apiprim->pos);
            api_release_thread(thread);
            return 0;
        } else {
            enum api_prim_execution_state st;
            gsvalue res;

            st = table->execs[apiprim->p.index](thread, apiprim, &thread->api_prim_blocking, &res);
            switch (st) {
                case api_st_success:
                    api_update_promise(thread->code->instrs[thread->code->ip].presult, res);
                    thread->code->ip++;
                    thread->api_prim_blocking = 0;
                    if (thread->code->ip >= thread->code->size)
                        api_done(thread)
                    ; else
                        thread->state = api_thread_st_active
                    ;
                    api_release_thread(thread);
                    return 1;
                case api_st_error:
                    /* We assume the exec function called api_abend */
                    api_release_thread(thread);
                    return 0;
                case api_st_blocked:
                    /* Loop and try again next time */
                    thread->state = api_thread_st_active;
                    api_release_thread(thread);
                    return 0;
                default:
                    api_abend(thread, UNIMPL("API instruction execution with state %d"), st);
                    api_release_thread(thread);
                    return 0;
            }
        }
    } else {
        api_take_thread(thread);
        api_abend(thread, UNIMPL("API instruction execution (%s)"), block->class->description);
        api_release_thread(thread);
        return 0;
    }
}

static
void
api_exec_err(struct api_thread *thread, gsvalue instr, gstypecode st)
{
    switch (st) {
        case gstyerr: {
            struct gserror *p;

            p = (struct gserror *)instr;
            switch (p->type) {
                case gserror_undefined:
                    api_abend(thread, "%P: undefined", p->pos);
                    break;
                case gserror_generated: {
                    struct gserror_message *msg = (struct gserror_message *)p;
                    api_abend(thread, "%P: %s", p->pos, msg->message);
                    break;
                }
                default:
                    api_abend(thread, "%P: unknown error type %d", p->pos, p->type);
                    break;
            }
            break;
        }
        case gstyimplerr: {
            char err[0x100];

            gsimplementation_failure_format(err, err + sizeof(err), (struct gsimplementation_failure *)instr);
            api_abend(thread, "%s", err);
            break;
        }
        default:
            api_abend_unimpl(thread, __FILE__, __LINE__, "API instruction execution (%d)", st);
            break;
    }
}

static
void
api_update_promise(struct api_promise *promise, gsvalue v)
{
    lock(&promise->lock);
    promise->value = v;
    unlock(&promise->lock);
}

static struct api_promise *api_alloc_promise(void);

#define CHECK_NSTATEMENTS() \
    do { \
        if (nstatements >= MAX_NUM_REGISTERS) { \
            api_abend_unimpl(thread, __FILE__, __LINE__, "%P: api_unpack_block_statement: too many statements", pinstr->pos, pinstr->instr); \
            return; \
        } \
    } while (0)

#define CHECK_NREGS(a_pos) \
    do { \
        if (nregs >= MAX_NUM_REGISTERS) { \
            api_abend_unimpl(thread, __FILE__, __LINE__, "%P: api_unpack_block_statement: too many registers", a_pos); \
            return; \
        } \
    } while (0)

#define CHECK_REG(regno) \
    do { \
        if (regno >= nregs) { \
            api_abend_unimpl(thread, __FILE__, __LINE__, "%P: api_unpack_block_statement: register %d out of bounds; %d registers so far", pinstr->pos, pinstr->instr, regno, nregs); \
            return; \
        } \
    } while (0)

static
void
api_unpack_block_statement(struct api_thread *thread, struct gsclosure *cl)
{
    void *pin;
    struct gsbco *code;
    struct gsbco **psubexpr;
    gsvalue *pglobal;
    struct gsbc *pinstr;
    struct gsbco *subexprs[MAX_NUM_REGISTERS];

    int nregs;
    gsvalue regs[MAX_NUM_REGISTERS];

    int nstatements;
    struct gspos poss[MAX_NUM_REGISTERS];
    gsvalue rhss[MAX_NUM_REGISTERS];
    struct api_promise *lhss[MAX_NUM_REGISTERS];
    int i;

    code = cl->cl.code;
    pin = (uchar*)code + sizeof(*code);

    nregs = 0;

    for (i = 0; i < code->numsubexprs; i++) {
        psubexpr = (struct gsbco **)pin;
        subexprs[i] = *psubexpr++;
        pin = psubexpr;
    }

    for (i = 0; i < code->numglobals; i++) {
        CHECK_NREGS(code->pos);
        pglobal = (gsvalue *)pin;
        regs[nregs++] = *pglobal++;
        pin = pglobal;
    }

    if (code->numfvs + code->numargs > cl->cl.numfvs) {
        api_abend_unimpl(thread, __FILE__, __LINE__, "%P: Can't actually enter %P: closure has %d free variables, but code has %d free variables and %d arguments = %d values needed", cl->hp.pos, code->pos, cl->cl.numfvs, code->numfvs, code->numargs, code->numfvs + code->numargs);
        return;
    }

    for (i = 0; i < code->numfvs + code->numargs; i++) {
        CHECK_NREGS(code->pos);
        regs[nregs++] = cl->cl.fvs[i];
    }

    nstatements = 0;
    for (;;) {
        pinstr = (struct gsbc *)pin;
        switch (pinstr->instr) {
            case gsbc_op_closure: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                CHECK_NREGS(pinstr->pos);

                subexpr = subexprs[ACE_CLOSURE_CODE(pinstr)];

                cl = gsreserveheap(sizeof(*cl) + ACE_CLOSURE_NUMFVS(pinstr) * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->cl.code = subexpr;
                cl->cl.numfvs = ACE_CLOSURE_NUMFVS(pinstr);
                for (i = 0; i < ACE_CLOSURE_NUMFVS(pinstr); i++) {
                    CHECK_REG(ACE_CLOSURE_FV(pinstr, i));
                    cl->cl.fvs[i] = regs[ACE_CLOSURE_FV(pinstr, i)];
                }
                regs[nregs] = (gsvalue)cl;
                nregs++;

                pin = ACE_CLOSURE_SKIP(pinstr);
                continue;
            }
            case gsbc_op_undefined: {
                CHECK_NREGS(pinstr->pos);

                regs[nregs] = (gsvalue)gsundefined(pinstr->pos);
                nregs++;
                pin = ACE_UNDEFINED_SKIP(pinstr);
                continue;
            }
            case gsbc_op_alias: {
                CHECK_NREGS(pinstr->pos);

                CHECK_REG(ACE_BODY_ALIAS_SOURCE(pinstr));
                regs[nregs++] = regs[ACE_BODY_ALIAS_SOURCE(pinstr)];

                pin = ACE_ALIAS_SKIP(pinstr);
                continue;
            }
            case gsbc_op_apply: {
                gsvalue fun, args[MAX_NUM_REGISTERS];

                CHECK_NREGS(pinstr->pos);

                fun = regs[ACE_APPLY_FUN(pinstr)];
                for (i = 0; i < ACE_APPLY_NUM_ARGS(pinstr); i++) args[i] = regs[ACE_APPLY_ARG(pinstr, i)];
                regs[nregs++] = gsnapplyv(pinstr->pos, fun, ACE_APPLY_NUM_ARGS(pinstr), args);

                pin = ACE_APPLY_SKIP(pinstr);
                continue;
            }
            case gsbc_op_lfield: {
                CHECK_NREGS(pinstr->pos);
                regs[nregs++] = gslfield(pinstr->pos, ACE_LFIELD_FIELD(pinstr), regs[ACE_LFIELD_RECORD(pinstr)]);
                pin = ACE_LFIELD_SKIP(pinstr);
                continue;
            }
            case gsbc_op_bind_closure: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                CHECK_NSTATEMENTS();
                CHECK_NREGS(pinstr->pos);

                subexpr = subexprs[ACE_BIND_CLOSURE_CODE(pinstr)];

                cl = gsreserveheap(sizeof(*cl) + ACE_BIND_CLOSURE_NUMFVS(pinstr) * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->cl.code = subexpr;
                cl->cl.numfvs = ACE_BIND_CLOSURE_NUMFVS(pinstr);
                for (i = 0; i < ACE_BIND_CLOSURE_NUMFVS(pinstr); i++)
                    cl->cl.fvs[i] = regs[ACE_BIND_CLOSURE_FV(pinstr, i)]
                ;
                rhss[nstatements] = (gsvalue)cl;
                poss[nstatements] = pinstr->pos;
                lhss[nstatements] = api_alloc_promise();
                regs[nregs] = (gsvalue)lhss[nstatements];

                nstatements++;
                nregs++;

                pin = ACE_BIND_CLOSURE_SKIP(pinstr);
                continue;
            }
            case gsbc_op_bind_apply: {
                gsvalue fun, args[MAX_NUM_REGISTERS];

                CHECK_NSTATEMENTS();
                CHECK_NREGS(pinstr->pos);

                fun = regs[ACE_BIND_APPLY_FUN(pinstr)];
                for (i = 0; i < ACE_BIND_APPLY_NUM_ARGS(pinstr); i++) args[i] = regs[ACE_BIND_APPLY_ARG(pinstr, i)];

                rhss[nstatements] = gsnapplyv(pinstr->pos, fun, ACE_BIND_APPLY_NUM_ARGS(pinstr), args);
                poss[nstatements] = pinstr->pos;
                lhss[nstatements] = api_alloc_promise();
                regs[nregs] = (gsvalue)lhss[nstatements];

                nstatements++;
                nregs++;

                pin = ACE_BIND_APPLY_SKIP(pinstr);
                continue;
            }
            case gsbc_op_body_closure: {
                struct gsbco *subexpr;
                struct gsclosure *cl;

                CHECK_NSTATEMENTS();

                subexpr = subexprs[ACE_BODY_CLOSURE_CODE(pinstr)];

                cl = gsreserveheap(sizeof(*cl) + ACE_BODY_CLOSURE_NUMFVS(pinstr) * sizeof(gsvalue));

                memset(&cl->hp.lock, 0, sizeof(cl->hp.lock));
                cl->hp.pos = pinstr->pos;
                cl->hp.type = gsclosure;
                cl->cl.code = subexpr;
                cl->cl.numfvs = ACE_BODY_CLOSURE_NUMFVS(pinstr);
                for (i = 0; i < ACE_BODY_CLOSURE_NUMFVS(pinstr); i++)
                    cl->cl.fvs[i] = regs[ACE_BODY_CLOSURE_FV(pinstr, i)]
                ;
                rhss[nstatements] = (gsvalue)cl;
                poss[nstatements] = pinstr->pos;
                nstatements++;
                goto got_statements;
            }
            case gsbc_op_body_undefined: {
                CHECK_NSTATEMENTS();
                rhss[nstatements] = (gsvalue)gsundefined(pinstr->pos);
                poss[nstatements] = pinstr->pos;
                nstatements++;
                goto got_statements;
            }
            case gsbc_op_body_alias: {
                CHECK_NSTATEMENTS();
                CHECK_REG(ACE_BODY_ALIAS_SOURCE(pinstr));
                rhss[nstatements] = regs[ACE_BODY_ALIAS_SOURCE(pinstr)];
                poss[nstatements] = pinstr->pos;
                nstatements++;
                goto got_statements;
            }
            case gsbc_op_body_apply: {
                gsvalue fun, args[MAX_NUM_REGISTERS];

                CHECK_NSTATEMENTS();

                fun = regs[ACE_BODY_APPLY_FUN(pinstr)];
                for (i = 0; i < ACE_BODY_APPLY_NUM_ARGS(pinstr); i++) args[i] = regs[ACE_BODY_APPLY_ARG(pinstr, i)];

                rhss[nstatements] = gsnapplyv(pinstr->pos, fun, ACE_BODY_APPLY_NUM_ARGS(pinstr), args);
                poss[nstatements] = pinstr->pos;
                nstatements++;

                goto got_statements;
            }
            default:
                api_abend_unimpl(thread, __FILE__, __LINE__, "%P: api_unpack_block_statement: %d opcodes", pinstr->pos, pinstr->instr);
                return;
        }
    }

got_statements:
    nstatements--;
    thread->code->instrs[thread->code->ip].instr = rhss[nstatements];
    thread->code->instrs[thread->code->ip].pos = poss[nstatements];
    while (nstatements--) {
        if (thread->code->ip <= 0) {
            api_abend_unimpl(thread, __FILE__, __LINE__, "code segment overflow");
            return;
        }
        thread->code->ip--;
        thread->code->instrs[thread->code->ip].instr = rhss[nstatements];
        thread->code->instrs[thread->code->ip].pos = poss[nstatements];
        thread->code->instrs[thread->code->ip].presult = lhss[nstatements];
    }
}

/* §section Adding threads */

static struct api_code_segment *api_alloc_code_segment(struct gspos, struct api_thread *, gsvalue);

struct api_thread *
api_add_thread(struct gspos pos, struct api_thread_table *api_thread_table, void *main_thread_data, struct api_prim_table *api_prim_table, gsvalue entry)
{
    int i;
    struct api_thread *thread;

    thread = 0;
    for (i = 0; i < API_NUMTHREADS; i++) {
        api_take_thread_queue();
        thread = &api_thread_queue->threads[i];
        api_release_thread_queue();
        api_take_thread(thread);
        if (api_thread_queue->threads[i].state == api_thread_st_unused) {
            thread = &api_thread_queue->threads[i];
            api_thread_queue->numthreads++;
            goto have_thread;
        } else {
            api_release_thread(thread);
        }
    }
    gsfatal(UNIMPL("thread queue overflow"));

have_thread:

    if (gsflag_stat_collection) {
        thread->start_time = nsec();
        thread->prog_term_time = 0;
    }
    thread->state = api_thread_st_active;
    thread->ismain = 0;

    thread->api_thread_table = api_thread_table;
    thread->api_prim_table = api_prim_table;
    thread->client_data = main_thread_data;
    thread->status = 0;

    thread->code = api_alloc_code_segment(pos, thread, entry);
    thread->api_prim_blocking = 0;

    return thread;
}

/* §section Allocation */

#define API_CODE_SEGMENT_BLOCK_SIZE sizeof(gsvalue) * 0x400

static struct gs_sys_aligned_block_suballoc_info api_code_segment_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "API code segments",
    },
    /* align = */ API_CODE_SEGMENT_BLOCK_SIZE,
};

static struct api_code_segment *api_new_code_segment(void);

static
struct api_code_segment *
api_alloc_code_segment(struct gspos pos, struct api_thread *thread, gsvalue entry)
{
    struct api_code_segment *res;

    res = api_new_code_segment();

    res->ip = res->size - 1;
    res->instrs[res->ip].instr = entry;
    res->instrs[res->ip].pos = pos;
    res->instrs[res->ip].presult = api_alloc_promise();

    return res;
}

static
struct api_code_segment *
api_new_code_segment()
{
    void *buf, *bufend;
    struct api_code_segment *res;

    gs_sys_aligned_block_suballoc(&api_code_segment_info, &buf, &bufend);
    res = buf;

    res->size = ((uchar*)bufend - (uchar*)res->instrs) / sizeof(res->instrs[0]);
    res->fwd = 0;

    return res;
}

static int api_gc_trace_promise(struct gsstringbuilder *, struct api_promise **);

static
int
api_gc_trace_code_segment(struct gsstringbuilder *err, struct api_code_segment **ppcode)
{
    struct api_code_segment *code, *newcode;
    gsvalue gctemp;
    int i;

    code = *ppcode;

    if (code->fwd) {
        *ppcode = code->fwd;
        return 0;
    }
    if (!gs_sys_block_in_gc_from_space(code)) return 0;

    if (code->ip < 0) {
        gsstring_builder_print(err, "%p: instruction pointer is negative: %d", code, code->ip);
        return -1;
    }
    if (code->ip > code->size) {
        gsstring_builder_print(err, "%p: instruction pointer is too large: %d > %d", code, code->ip, code->size);
        return -1;
    }

    newcode = api_new_code_segment();

    if (newcode->size < code->size - code->ip) {
        gsstring_builder_print(err, UNIMPL("api_gc_trace_code_segment: not enough space for all instructions"));
        return -1;
    }
    newcode->ip = newcode->size - (code->size - code->ip); /* > newcode->size - newcode->ip = code->size - code->ip; */
    memcpy(newcode->instrs + newcode->ip, code->instrs + code->ip, sizeof(struct api_instr)*(code->size - code->ip));

    code->fwd = newcode;

    for (i = newcode->ip; i < newcode->size; i++) {
        if (gs_gc_trace_pos(err, &newcode->instrs[i].pos) < 0) return -1;
        if (GS_GC_TRACE(err, &newcode->instrs[i].instr) < 0) return -1;
        if (api_gc_trace_promise(err, &newcode->instrs[i].presult) < 0) return -1;
    }

    *ppcode = newcode;
    return 0;
}

static gstypecode api_promise_eval(struct gspos, gsvalue);
static gsvalue api_promise_dereference(struct gspos, gsvalue);
static gsvalue api_promise_trace(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info api_promise_info = {
    /* descr = */ {
        /* evaluator = */ api_promise_eval,
        /* indirection_dereferencer = */ api_promise_dereference,
        /* gc_trace = */ api_promise_trace,
        /* description = */ "API promises",
    },
};

static
struct api_promise *
api_alloc_promise()
{
    struct api_promise *res;

    res = gs_sys_global_block_suballoc(&api_promise_info, sizeof(*res));

    memset(res, 0, sizeof(*res));

    return res;
}

static
gstypecode
api_promise_eval(struct gspos pos, gsvalue val)
{
    struct api_promise *promise;
    gstypecode res;

    promise = (struct api_promise *)val;
    lock(&promise->lock);
    res = promise->value ? gstyindir : gstyblocked;
    unlock(&promise->lock);

    return res;
}

static
gsvalue
api_promise_dereference(struct gspos pos, gsvalue val)
{
    struct api_promise *promise;
    gsvalue res;

    promise = (struct api_promise *)val;
    lock(&promise->lock);
    res = promise->value;
    unlock(&promise->lock);

    return res;
}

static
gsvalue
api_promise_trace(struct gsstringbuilder *err, gsvalue v)
{
    struct api_promise *promise;

    promise = (struct api_promise *)v;

    if (promise->evacuating) {
        gsstring_builder_print(err, UNIMPL("api_promise_trace: loops during GC"));
        return 0;
    }

    if (promise->value) {
        gsvalue value, gctemp;
        promise->evacuating = 1;
        value = promise->value;
        if (GS_GC_TRACE(err, &value) < 0) {
            promise->evacuating = 0;
            return 0;
        }
        promise->evacuating = 0;
        return value;
    }

    if (api_gc_trace_promise(err, &promise) < 0) return 0;

    return (gsvalue)promise;
}

static
int
api_gc_trace_promise(struct gsstringbuilder *err, struct api_promise **ppromise)
{
    struct api_promise *promise, *newpromise;

    promise = *ppromise;

    if (promise->fwd) {
        *ppromise = promise->fwd;
        return 0;
    }
    if (!gs_sys_block_in_gc_from_space(promise)) return 0;

    newpromise = api_alloc_promise();

    newpromise->value = promise->value;

    promise->fwd = newpromise;

    if (newpromise->value) {
        gsstring_builder_print(err, UNIMPL("api_gc_trace_promise: evacuate value"));
        return -1;
    }

    *ppromise = newpromise;
    return 0;
}

/* §section Helper Functions */

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

int
api_thread_terminating(struct api_thread *thread)
{
    int res;

    api_take_thread(thread);
    res = thread->state == api_thread_st_terminating_on_done || thread->state == api_thread_st_terminating_on_abend;
    api_release_thread(thread);

    return res;
}

void
api_done(struct api_thread *thread)
{
    thread->state = api_thread_st_terminating_on_done;

    thread->status = gsreserve_string_builder();
    gsfinish_string_builder(thread->status);
}

void
api_abend(struct api_thread *thread, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    thread->state = api_thread_st_terminating_on_abend;
    thread->status = gsreserve_string_builder();
    gsstring_builder_print(thread->status, "%s", buf);
    gsfinish_string_builder(thread->status);
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

void
api_thread_post(struct api_thread *thread, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    lock(&thread->lock);
    api_abend(thread, "%s", buf);
    unlock(&thread->lock);
}

void
api_thread_post_unimpl(struct api_thread *thread, char *file, int lineno, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf+sizeof buf, msg, arg);
    va_end(arg);

    lock(&thread->lock);
    if (gsdebug)
        api_abend(thread, "%s:%d: %s next", file, lineno, buf)
    ; else
        api_abend(thread, "Panic!  Unimplemented operation '%s' in release build", buf)
    ;
    unlock(&thread->lock);
}

/* §section API primitive implementations */

enum api_prim_execution_state
api_thread_handle_prim_unit(struct api_thread *thread, struct gsapiprim *apiprim, struct api_prim_blocking **pblocking, gsvalue *res)
{
    *res = apiprim->p.arguments[0];
    return api_st_success;
}

/* §section API Primitive Blocking/Restoring Data */

static struct gs_sys_global_block_suballoc_info api_blocking_info_info = {
    /* descr = */{
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "API Primitive Blocking/Restoring Data",
    },
};

void *
api_blocking_alloc(ulong sz, api_prim_blocking_gccopy *gccopy, api_prim_blocking_gcevacuate *gcevacuate, api_prim_blocking_gccleanup *gccleanup)
{
    struct api_prim_blocking *res;

    res = gs_sys_global_block_suballoc(&api_blocking_info_info, sz);
    res->forward = 0;
    res->gccopy = gccopy;
    res->gcevacuate = gcevacuate;
    res->gccleanup = gccleanup;

    return res;
}
