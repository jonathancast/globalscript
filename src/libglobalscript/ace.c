#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "gsbytecode.h"
#include "ace.h"

gsvalue gsentrypoint;
struct gstype *gsentrytype;

struct ace_thread_pool_args {
};

static struct ace_thread_queue *ace_thread_queue;

static struct gs_sys_global_block_suballoc_info ace_thread_queue_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "ACE Thread Queue",
    },
};

static void ace_thread_pool_main(void *);

static gs_sys_gc_post_callback ace_thread_cleanup;

int
ace_init()
{
    int ace_pool;

    struct ace_thread_pool_args ace_thread_pool_args;

    if (ace_thread_queue)
        gsfatal("ace_init called twice")
    ;

    ace_thread_queue = gs_sys_global_block_suballoc(&ace_thread_queue_info, sizeof(*ace_thread_queue));

    memset(ace_thread_queue, 0, sizeof(*ace_thread_queue));

    gs_sys_gc_post_callback_register(ace_thread_cleanup);

    ace_up();

    if ((ace_pool = gscreate_thread_pool(ace_thread_pool_main, &ace_thread_pool_args, sizeof(ace_thread_pool_args))) < 0)
        gsfatal("Couldn't create ACE thread pool: %r")
    ;

    return 0;
}

struct ace_thread_pool_stats;

static int ace_find_thread(struct ace_thread_pool_stats *, int, struct ace_thread **);

static void ace_return(struct ace_thread *, struct gspos, gsvalue);
static void ace_error_thread(struct ace_thread *, struct gserror *);

static int ace_exec_efv(struct ace_thread *);
static int ace_exec_alloc(struct ace_thread *);
static int ace_exec_push(struct ace_thread *);
static int ace_exec_branch(struct ace_thread *);
static int ace_exec_terminal(struct ace_thread *);

static void ace_instr_perform_analyze(struct ace_thread *);
static void ace_instr_perform_danalyze(struct ace_thread *);
static void ace_instr_return_undef(struct ace_thread *);
static void ace_instr_enter(struct ace_thread *, struct ace_thread_pool_stats *);
static void ace_instr_yield(struct ace_thread *);
static void ace_instr_ubprim(struct ace_thread *);
static void ace_instr_lprim(struct ace_thread *);

struct ace_thread_pool_stats {
    vlong numthreads_total, num_blocked, num_blocked_threads, num_blocks_on_function, num_blocks_on_new_stack, num_blocks_on_existing_thread;
    vlong gc_time, checking_thread_time;
};

static
void
ace_thread_pool_main(void *p)
{
    int tid;
    int nwork;
    struct ace_thread_pool_stats stats;
    vlong outer_loops, outer_loops_without_threads, total_thread_load, num_timeslots, num_completed_timeslots, num_blocked_timeslots, num_finished_timeslots, num_instrs;
    vlong start_time, end_time, finding_thread_time, finding_thread_start_time, instr_time, instr_start_time, waiting_for_thread_time, waiting_for_thread_start_time;

    outer_loops = outer_loops_without_threads = total_thread_load = stats.numthreads_total = num_instrs = stats.num_blocked = stats.num_blocked_threads = num_timeslots = num_completed_timeslots = num_blocked_timeslots = num_finished_timeslots = stats.num_blocks_on_function = stats.num_blocks_on_new_stack = stats.num_blocks_on_existing_thread = 0;
    start_time = nsec();
    finding_thread_time = instr_time = stats.gc_time = stats.checking_thread_time = waiting_for_thread_time = waiting_for_thread_start_time = 0;
    tid = 0;
    for (;;) {
        struct ace_thread *thread;
        outer_loops++;

        finding_thread_start_time = gsflag_stat_collection ? nsec() : 0;
        if (ace_find_thread(&stats, tid, &thread) < 0) goto no_clients;
        if (gsflag_stat_collection) finding_thread_time += nsec() - finding_thread_start_time;

        if (gsflag_stat_collection && waiting_for_thread_start_time && thread) {
            waiting_for_thread_time = nsec() - waiting_for_thread_start_time;
            waiting_for_thread_start_time = 0;
        }

        nwork = 0;
        if (thread && thread->state == ace_thread_running) num_timeslots++;
        instr_start_time = gsflag_stat_collection ? nsec() : 0;
        if (thread) while (thread->state == ace_thread_running && nwork < 0x1000) {
            while (ace_exec_efv(thread)) nwork++, num_instrs++;
            while (ace_exec_alloc(thread)) nwork++, num_instrs++;
            while (ace_exec_push(thread)) nwork++, num_instrs++;
            if (ace_exec_branch(thread)) nwork++, num_instrs++;
            else if (ace_exec_terminal(thread)) nwork++, num_instrs++;
            else {
                nwork++, num_instrs++;
                switch (thread->st.running.ip->instr) {
                    case gsbc_op_analyze:
                        ace_instr_perform_analyze(thread);
                        break;
                    case gsbc_op_danalyze:
                        ace_instr_perform_danalyze(thread);
                        break;
                    case gsbc_op_undef:
                        ace_instr_return_undef(thread);
                        break;
                    case gsbc_op_enter:
                        ace_instr_enter(thread, &stats);
                        break;
                    case gsbc_op_yield:
                        ace_instr_yield(thread);
                        break;
                    case gsbc_op_ubprim:
                        ace_instr_ubprim(thread);
                        break;
                    case gsbc_op_lprim:
                        ace_instr_lprim(thread);
                        break;
                    default:
                        ace_thread_unimpl(thread, __FILE__, __LINE__, thread->st.running.ip->pos, "run instruction %d", thread->st.running.ip->instr);
                        break;
                }
            }
        }
        if (gsflag_stat_collection) instr_time += nsec() - instr_start_time;
        if (thread && thread->state == ace_thread_running) num_completed_timeslots++;
        if (thread && thread->state == ace_thread_blocked) num_blocked_timeslots++;
        if (thread && thread->state == ace_thread_finished) num_finished_timeslots++;

        if (thread) unlock(&thread->lock);

        lock(&ace_thread_queue->lock);
        total_thread_load += ace_thread_queue->num_active_threads;
        if (ace_thread_queue->num_active_threads) {
            tid++;
            if (tid >= ace_thread_queue->num_active_threads) tid = 0;
            unlock(&ace_thread_queue->lock);
        } else {
            unlock(&ace_thread_queue->lock);
            if (gsflag_stat_collection && !waiting_for_thread_start_time) waiting_for_thread_start_time = nsec();
            outer_loops_without_threads++;
        }
    }
no_clients:
    end_time = nsec();

    if (gsflag_stat_collection) {
        gsstatprint("# ACE threads: %d\n", ace_thread_queue->numthreads);
        if (ace_thread_queue->numthreads) gsstatprint("Avg # instructions / ACE thread: %d\n", num_instrs / ace_thread_queue->numthreads);
        gsstatprint("# ACE outer loops: %lld\n", outer_loops);
        if (outer_loops) gsstatprint("Avg # ACE threads: %02g\n", (double)stats.numthreads_total / outer_loops);
        if (outer_loops) gsstatprint("Avg ACE system load: %02g\n", (double)total_thread_load / outer_loops);
        if (outer_loops) gsstatprint("ACE %% loops w/ no threads: %02g%%\n", ((double)outer_loops_without_threads / outer_loops) * 100);
        if (stats.numthreads_total) gsstatprint("ACE threads: %2.2g%% blocked, %2.2g%% blocked on threads\n", ((double)stats.num_blocked / stats.numthreads_total) * 100, ((double)stats.num_blocked_threads / stats.numthreads_total) * 100);
        gsstatprint("ACE Run time: %llds %lldms\n", (end_time - start_time) / 1000 / 1000 / 1000, ((end_time - start_time) / 1000 / 1000) % 1000);
        gsstatprint("ACE Finding thread time: %llds %lldms\n", finding_thread_time / 1000 / 1000 / 1000, (finding_thread_time / 1000 / 1000) % 1000);
        gsstatprint("ACE Checking thread state time: %llds %lldms\n", stats.checking_thread_time / 1000 / 1000 / 1000, (stats.checking_thread_time / 1000 / 1000) % 1000);
        gsstatprint("ACE instruction execution time: %llds %lldms\n", instr_time / 1000 / 1000 / 1000, (instr_time / 1000 / 1000) % 1000);
        gsstatprint("ACE avg time slot time: %lldns\n", instr_time / num_timeslots);
        gsstatprint("GC time: %llds %lldms\n", stats.gc_time / 1000 / 1000 / 1000, (stats.gc_time / 1000 / 1000) % 1000);
        if (num_instrs) gsstatprint("Avg unit of work: %gμs\n", (double)instr_time / num_instrs / 1000);
        if (num_timeslots) gsstatprint(
            "Time slots: %lld "
                "(%02g%% ran to completion; "
                "%02g%% blocked; "
                "%02g%% finished; "
                "%02g%% blocked on functions; "
                "%02g%% blocked on new stacks; "
                "%02g%% blocked on existing threads)\n",
            num_timeslots,
            (double)num_completed_timeslots / num_timeslots * 100,
            (double)num_blocked_timeslots / num_timeslots * 100,
            (double)num_finished_timeslots / num_timeslots * 100,
            (double)stats.num_blocks_on_function / num_timeslots * 100,
            (double)stats.num_blocks_on_new_stack / num_timeslots * 100,
            (double)stats.num_blocks_on_existing_thread / num_timeslots * 100
        );
        gsstatprint("Time waiting for a thread: %llds %lldms\n", waiting_for_thread_time / 1000 / 1000 / 1000, (waiting_for_thread_time / 1000 / 1000) % 1000);
    }
}

int
ace_find_thread(struct ace_thread_pool_stats *stats, int tid, struct ace_thread **pthread)
{
    struct ace_thread *thread;
    vlong gc_start_time, checking_thread_start_time;

    thread = 0;

    gc_start_time = gsflag_stat_collection ? nsec() : 0;
    if (gs_sys_gc_allow_collection(0) < 0) return -1;
    if (gsflag_stat_collection) stats->gc_time += nsec() - gc_start_time;

    lock(&ace_thread_queue->lock);
        if (ace_thread_queue->refcount <= 0) {
            unlock(&ace_thread_queue->lock);
            return -1;
        }
        thread = ace_thread_queue->threads[tid];
    unlock(&ace_thread_queue->lock);

    if (thread) {
        checking_thread_start_time = gsflag_stat_collection ? nsec() : 0;
        stats->numthreads_total++;
        lock(&thread->lock);
        switch (thread->state) {
            case ace_thread_lprim_blocked: {
                thread->st.lprim_blocked.on->resume(thread, thread->st.lprim_blocked.at, thread->st.lprim_blocked.on);
                if (thread->state != ace_thread_running) {
                    unlock(&thread->lock);
                    thread = 0;
                }
                break;
            }
            case ace_thread_blocked: {
                gstypecode st;

            again:
                st = GS_SLOW_EVALUATE(thread->st.blocked.at, thread->st.blocked.on);

                switch (st) {
                    case gstystack:
                        stats->num_blocked_threads++;
                    case gstyblocked:
                        stats->num_blocked++;
                        break;
                    case gstyindir:
                        thread->st.blocked.on = GS_REMOVE_INDIRECTION(thread->st.blocked.at, thread->st.blocked.on);
                        goto again;
                        break;
                    case gstywhnf: {
                        ace_return(thread, thread->st.blocked.at, thread->st.blocked.on);
                        break;
                    }
                    case gstyerr: {
                        ace_error_thread(thread, (struct gserror *)thread->st.blocked.on);
                        break;
                    }
                    case gstyimplerr: {
                        ace_failure_thread(thread, (struct gsimplementation_failure *)thread->st.blocked.on);
                        break;
                    }
                    case gstyunboxed: {
                        ace_return(thread, thread->st.blocked.at, thread->st.blocked.on);
                        break;
                    }
                    case gstyeoothreads: {
                        ace_thread_unimpl(thread, __FILE__, __LINE__, thread->st.blocked.at, ".enter: out of threads");
                        break;
                    }
                    default:
                        ace_thread_unimpl(thread, __FILE__, __LINE__, thread->st.blocked.at, ".enter (st = %d)", st);
                        break;
                }
                if (thread->state != ace_thread_running) {
                    unlock(&thread->lock);
                    thread = 0;
                }
                break;
            }
            case ace_thread_running:
                break;
            case ace_thread_finished:
                unlock(&thread->lock);
                thread = 0;
                break;
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, thread->st.running.ip->pos, "ace_thread_pool_main: state %d", thread->state);
                unlock(&thread->lock);
                thread = 0;
                break;
        }
        if (gsflag_stat_collection) stats->checking_thread_time += nsec() - checking_thread_start_time;
    }

    *pthread = thread;

    return 0;
}

int
ace_thread_cleanup(struct gsstringbuilder *err)
{
    struct ace_thread_queue *new_queue;
    int i;
    int srcthread, destthread;

    new_queue = gs_sys_global_block_suballoc(&ace_thread_queue_info, sizeof(*ace_thread_queue));

    memset(&new_queue->lock, 0, sizeof(new_queue->lock));
    new_queue->refcount = ace_thread_queue->refcount;
    new_queue->numthreads = ace_thread_queue->numthreads;

    for (i = 0; i < NUM_ACE_THREADS; i++)
        new_queue->threads[i] = ace_thread_queue->threads[i]
    ;

    ace_thread_queue = new_queue;

    destthread = 0;
    for (srcthread = 0; srcthread < NUM_ACE_THREADS; srcthread++) {
        if (ace_thread_queue->threads[srcthread]) {
            if (!gs_sys_block_in_gc_from_space(ace_thread_queue->threads[srcthread])) {
                ace_thread_queue->threads[destthread] = ace_thread_queue->threads[srcthread];
                ace_thread_queue->threads[destthread]->tid = destthread;
                destthread++;
            } else if (ace_thread_queue->threads[srcthread]->state == ace_thread_gcforward) {
                struct ace_thread *thread;

                thread = ace_thread_queue->threads[destthread] = ace_thread_queue->threads[srcthread]->st.forward.dest;
                ace_thread_queue->threads[destthread]->tid = destthread;
                destthread++;

                if (ace_stack_post_gc_consolidate(err, thread) < 0) return -1;
            }
        }
    }
    ace_thread_queue->num_active_threads = destthread;

    for (; destthread < NUM_ACE_THREADS; destthread++) ace_thread_queue->threads[destthread] = 0;

    return 0;
}

static void ace_instr_extract_efv(struct ace_thread *);

int
ace_exec_efv(struct ace_thread *thread)
{
    switch (thread->st.running.ip->instr) {
        case gsbc_op_efv:
            ace_instr_extract_efv(thread);
            return 1;
        default:
            return 0;
    }
}

static
void
ace_instr_extract_efv(struct ace_thread *thread)
{
    struct gsbc *ip;
    gstypecode st;
    int nreg;

    ip = thread->st.running.ip;

    nreg = ACE_EFV_REGNUM(ip);

again:
    st = GS_SLOW_EVALUATE(ip->pos, thread->regs[nreg]);

    switch (st) {
        case gstywhnf:
            break;
        case gstyindir:
            thread->regs[nreg] = GS_REMOVE_INDIRECTION(ip->pos, thread->regs[nreg]);
            goto again;
        case gstyimplerr:
            ace_failure_thread(thread, (struct gsimplementation_failure *)thread->regs[nreg]);
            return;
        default:
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "ace_extract_efv: st = %d", st);
            return;
    }

    thread->st.running.ip = ACE_EFV_SKIP(ip);
    return;
}

static void ace_instr_alloc_closure(struct ace_thread *);
static void ace_instr_prim(struct ace_thread *);
static void ace_instr_alloc_constr(struct ace_thread *);
static void ace_instr_alloc_record(struct ace_thread *);
static void ace_instr_extract_field(struct ace_thread *);
static void ace_instr_alloc_lfield(struct ace_thread *);
static void ace_instr_alloc_undef(struct ace_thread *);
static void ace_instr_copy_alias(struct ace_thread *);
static void ace_instr_alloc_apply(struct ace_thread *);
static void ace_instr_alloc_unknown_api_prim(struct ace_thread *);
static void ace_instr_alloc_api_prim(struct ace_thread *);

int
ace_exec_alloc(struct ace_thread *thread)
{
    switch (thread->st.running.ip->instr) {
        case gsbc_op_closure:
            ace_instr_alloc_closure(thread);
            return 1;
        case gsbc_op_prim:
            ace_instr_prim(thread);
            return 1;
        case gsbc_op_constr:
            ace_instr_alloc_constr(thread);
            return 1;
        case gsbc_op_record:
            ace_instr_alloc_record(thread);
            return 1;
        case gsbc_op_field:
            ace_instr_extract_field(thread);
            return 1;
        case gsbc_op_lfield:
            ace_instr_alloc_lfield(thread);
            return 1;
        case gsbc_op_undefined:
            ace_instr_alloc_undef(thread);
            return 1;
        case gsbc_op_alias:
            ace_instr_copy_alias(thread);
            return 1;
        case gsbc_op_apply:
            ace_instr_alloc_apply(thread);
            return 1;
        case gsbc_op_unknown_api_prim:
            ace_instr_alloc_unknown_api_prim(thread);
            return 1;
        case gsbc_op_api_prim:
            ace_instr_alloc_api_prim(thread);
            return 1;
        default:
            return 0;
    }
}

void
ace_instr_alloc_closure(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsheap_item *hp;
    struct gsclosure *cl;
    int i;

    ip = thread->st.running.ip;

    hp = gsreserveheap(sizeof(struct gsclosure) + ACE_CLOSURE_NUMFVS(ip) * sizeof(gsvalue));
    cl = (struct gsclosure *)hp;

    memset(&hp->lock, 0, sizeof(hp->lock));
    hp->pos = ip->pos;
    hp->type = gsclosure;
    if (ACE_CLOSURE_CODE(ip) > thread->nsubexprs) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".closure subexpr out of range");
        return;
    }
    cl->cl.code = thread->subexprs[ACE_CLOSURE_CODE(ip)];
    cl->cl.numfvs = ACE_CLOSURE_NUMFVS(ip);
    if (cl->cl.numfvs > 0x100) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".closure with too many free variables", cl->cl.numfvs);
        return;
    }
    for (i = 0; i < ACE_CLOSURE_NUMFVS(ip); i++) {
        if (ACE_CLOSURE_FV(ip, i) > thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".closure free variable out of range");
            return;
        }
        cl->cl.fvs[i] = thread->regs[ACE_CLOSURE_FV(ip, i)];
    }
    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Register overflow in .closure");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)hp;
    thread->nregs++;
    thread->st.running.ip = ACE_CLOSURE_SKIP(ip);
    return;
}

static
void
ace_instr_prim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsregistered_primset *prims;
    gsvalue args[MAX_NUM_REGISTERS];
    int i;
    int res;

    ip = thread->st.running.ip;

    for (i = 0; i < ACE_PRIM_NARGS(ip); i++)
        args[i] = thread->regs[ACE_PRIM_ARG(ip, i)]
    ;
    prims = gsprims_lookup_prim_set_by_index(ACE_PRIM_PRIMSET_INDEX(ip));

    res = prims->exec_table[ACE_PRIM_INDEX(ip)](thread, ip->pos, ACE_PRIM_NARGS(ip), args, &thread->regs[thread->nregs++]);

    if (res)
        thread->st.running.ip = ACE_PRIM_SKIP(ip)
    ;

    return;
}

static
void
ace_instr_alloc_constr(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr *constr;
    int i;

    ip = thread->st.running.ip;

    constr = gsreserveconstrs(sizeof(*constr) + ACE_CONSTR_NUMARGS(ip) * sizeof(gsvalue));
    constr->pos = ip->pos;
    constr->type = gsconstr_args;
    constr->a.constrnum = ACE_CONSTR_CONSTRNUM(ip);
    constr->a.numargs = ACE_CONSTR_NUMARGS(ip);
    for (i = 0; i < ACE_CONSTR_NUMARGS(ip); i++)
        constr->a.arguments[i] = thread->regs[ACE_CONSTR_ARG(ip, i)]
    ;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs++] = (gsvalue)constr;

    thread->st.running.ip = ACE_CONSTR_SKIP(ip);

    return;
}

static
void
ace_instr_alloc_record(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsrecord *record;
    struct gsrecord_fields *fields;
    int j;

    ip = thread->st.running.ip;

    record = gsreserverecords(sizeof(*fields) + ip->args[0] * sizeof(gsvalue));
    fields = (struct gsrecord_fields *)record;
    record->pos = ip->pos;
    record->type = gsrecord_fields;
    fields->numfields = ACE_RECORD_NUMFIELDS(ip);
    for (j = 0; j < ip->args[0]; j++)
        fields->fields[j] = thread->regs[ACE_RECORD_FIELD(ip, j)]
    ;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)record;
    thread->nregs++;
    thread->st.running.ip = ACE_RECORD_SKIP(ip);
    return;
}

static
void
ace_instr_extract_field(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsrecord_fields *record;

    ip = thread->st.running.ip;

    record = (struct gsrecord_fields *)thread->regs[ACE_FIELD_RECORD(ip)];
    thread->regs[thread->nregs] = record->fields[ACE_FIELD_FIELD(ip)];
    thread->nregs++;
    thread->st.running.ip = ACE_FIELD_SKIP(ip);
    return;
}

static
void
ace_instr_alloc_lfield(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    thread->regs[thread->nregs++] = gslfield(ip->pos, ACE_LFIELD_FIELD(ip), thread->regs[ACE_LFIELD_RECORD(ip)]);
    thread->st.running.ip = ACE_LFIELD_SKIP(ip);
    return;
}

static
void
ace_instr_alloc_undef(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    thread->regs[thread->nregs] = (gsvalue)gsundefined(ip->pos);
    thread->nregs++;
    thread->st.running.ip = ACE_UNDEFINED_SKIP(ip);
    return;
}

static
void
ace_instr_copy_alias(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    thread->regs[thread->nregs] = thread->regs[ACE_ALIAS_SOURCE(ip)];
    thread->nregs++;
    thread->st.running.ip = ACE_ALIAS_SKIP(ip);
    return;
}

static
void
ace_instr_alloc_apply(struct ace_thread *thread)
{
    struct gsbc *ip;
    gsvalue fun, args[MAX_NUM_REGISTERS];
    int i;

    ip = thread->st.running.ip;

    fun = thread->regs[ACE_APPLY_FUN(ip)];

    for (i = 0; i < ACE_APPLY_NUM_ARGS(ip); i++)
        args[i] = thread->regs[ACE_APPLY_ARG(ip, i)];
    ;

    thread->regs[thread->nregs] = gsnapplyv(ip->pos, fun, ACE_APPLY_NUM_ARGS(ip), args);
    thread->nregs++;
    thread->st.running.ip = ACE_APPLY_SKIP(ip);
    return;
}

static
void
ace_instr_alloc_unknown_api_prim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;

    ip = thread->st.running.ip;

    prim = gsreserveeprims(sizeof(*prim));
    prim->pos = ip->pos;
    prim->type = eprim_prim;
    prim->p.numargs = 0;
    prim->p.index = -1;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)prim;
    thread->nregs++;
    thread->st.running.ip = GS_NEXT_BYTECODE(ip, 0);
    return;
}

static
void
ace_instr_alloc_api_prim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;
    int j;

    ip = thread->st.running.ip;

    prim = gsreserveeprims(sizeof(*prim) + ACE_IMPPRIM_NUMARGS(ip) * sizeof(gsvalue));
    prim->pos = ip->pos;
    prim->type = eprim_prim;
    prim->p.numargs = ACE_IMPPRIM_NUMARGS(ip);
    prim->p.index = ACE_IMPPRIM_INDEX(ip);
    for (j = 0; j < ACE_IMPPRIM_NUMARGS(ip); j++) {
        if (ACE_IMPPRIM_ARG(ip, j) >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".eprim argument too large");
            return;
        }
        prim->p.arguments[j] = thread->regs[ACE_IMPPRIM_ARG(ip, j)];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)prim;
    thread->nregs++;
    thread->st.running.ip = ACE_IMPPRIM_SKIP(ip);

    return;
}

static void ace_instr_push_app(struct ace_thread *);
static void ace_instr_push_force(struct ace_thread *);
static void ace_instr_push_strict(struct ace_thread *);
static void ace_instr_push_ubanalyze(struct ace_thread *);

int
ace_exec_push(struct ace_thread *thread)
{
    switch (thread->st.running.ip->instr) {
        case gsbc_op_app:
            ace_instr_push_app(thread);
            return 1;
        case gsbc_op_force:
            ace_instr_push_force(thread);
            return 1;
        case gsbc_op_strict:
            ace_instr_push_strict(thread);
            return 1;
        case gsbc_op_ubanalzye:
            ace_instr_push_ubanalyze(thread);
            return 1;
        default:
            return 0;
    }
}

static
void
ace_instr_push_app(struct ace_thread *thread)
{
    struct gsbc *ip;
    gsvalue args[MAX_NUM_REGISTERS];
    int j;

    ip = thread->st.running.ip;

    for (j = 0; j < ACE_APP_NUMARGS(ip); j++) {
        if (ACE_APP_ARG(ip, j) >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".app argument too large: arg #%d is %d (%d registers to this point)", j, ACE_APP_ARG(ip, j), thread->nregs);
            return;
        }
        args[j] = thread->regs[ACE_APP_ARG(ip, j)];
    }

    if (!ace_push_appv(ip->pos, thread, ACE_APP_NUMARGS(ip), args)) return;

    thread->st.running.ip = ACE_APP_SKIP(ip);
    return;
}

static
void
ace_instr_push_force(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct ace_cont *cont;
    struct gsbc_cont_force *force;
    int i;

    ip = thread->st.running.ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_force) + ACE_FORCE_NUMFVS(ip) * sizeof(gsvalue));
    force = (struct gsbc_cont_force *)cont;
    if (!cont) return;

    cont->node = gsbc_cont_force;
    cont->pos = ip->pos;
    force->code = thread->subexprs[ACE_FORCE_CONT(ip)];
    force->numfvs = ACE_FORCE_NUMFVS(ip);
    for (i = 0; i < ACE_FORCE_NUMFVS(ip); i++) {
        force->fvs[i] = thread->regs[ACE_FORCE_FV(ip, i)];
    }

    thread->st.running.ip = ACE_FORCE_SKIP(ip);
    return;
}

static
void
ace_instr_push_strict(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct ace_cont *cont;
    struct gsbc_cont_strict *strict;
    int i;

    ip = thread->st.running.ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_strict) + ACE_STRICT_NUMFVS(ip) * sizeof(gsvalue));
    strict = (struct gsbc_cont_strict *)cont;
    if (!cont) return;

    cont->node = gsbc_cont_strict;
    cont->pos = ip->pos;
    strict->code = thread->subexprs[ACE_STRICT_CONT(ip)];
    strict->numfvs = ACE_STRICT_NUMFVS(ip);
    for (i = 0; i < ACE_STRICT_NUMFVS(ip); i++)
        strict->fvs[i] = thread->regs[ACE_STRICT_FV(ip, i)]
    ;

    thread->st.running.ip = ACE_STRICT_SKIP(ip);
    return;
}

static
void
ace_instr_push_ubanalyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct ace_stack_ubanalyze_cont *pcont;
    int i;

    ip = thread->st.running.ip;

    ACE_STACK_UBANALYZE_PUSH(pcont, ip->pos, thread,
        ACE_UBANALYZE_NUMCONTS(ip), thread->subexprs[ACE_UBANALYZE_CONT(ip, i)],
        ACE_UBANALYZE_NUMFVS(ip), thread->regs[ACE_UBANALYZE_FV(ip, i)],
        return
    );

    thread->st.running.ip = ACE_UBANALYZE_SKIP(ip);
    return;
}

int
ace_exec_branch(struct ace_thread *thread)
{
    switch (thread->st.running.ip->instr) {
        default:
            return 0;
    }
}

static void ace_poison_thread(struct ace_thread *, struct gspos, char *, ...);

static
void
ace_instr_perform_analyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr *constr;
    struct gsbc **cases;

    int i;

    ip = thread->st.running.ip;

    constr = (struct gsconstr *)thread->regs[ACE_ANALYZE_SCRUTINEE(ip)];
    cases = ACE_ANALYZE_CASES(ip);

    thread->st.running.ip = ip = cases[constr->a.constrnum];
    for (i = 0; i < constr->a.numargs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_poison_thread(thread, ip->pos, "Register overflow");
            return;
        }
        thread->regs[thread->nregs++] = constr->a.arguments[i];
    }

    return;
}

static
void
ace_instr_perform_danalyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr *constr;
    struct gsbc **cases;
    int casenum;

    int i;

    ip = thread->st.running.ip;

    constr = (struct gsconstr *)thread->regs[ACE_DANALYZE_SCRUTINEE(ip)];
    cases = ACE_DANALYZE_CASES(ip);

    casenum = 0;
    for (i = 0; i < ACE_DANALYZE_NUMCONSTRS(ip); i++) {
        if (ACE_DANALYZE_CONSTR(ip, i) == constr->a.constrnum) {
            casenum = 1 + i;
            break;
        }
    }

    thread->st.running.ip = ip = cases[casenum];
    if (casenum > 0) {
        for (i = 0; i < constr->a.numargs; i++) {
            if (thread->nregs >= MAX_NUM_REGISTERS) {
                ace_poison_thread(thread, ip->pos, "Register overflow");
                return;
            }
            thread->regs[thread->nregs++] = constr->a.arguments[i];
        }
    }

    return;
}

int
ace_exec_terminal(struct ace_thread *thread)
{
    switch (thread->st.running.ip->instr) {
        default:
            return 0;
    }
}

static
void
ace_instr_return_undef(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    ace_error_thread(thread, gsundefined(ip->pos));
}

static int ace_thread_enter_closure(struct gspos, struct ace_thread *, struct gsheap_item *, struct ace_thread_pool_stats *);

static
void
ace_instr_enter(struct ace_thread *thread, struct ace_thread_pool_stats *stats)
{
    struct gsbc *ip;
    gstypecode st;
    gsvalue prog;

    ip = thread->st.running.ip;

    if (ACE_ENTER_ARG(ip) >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ACE_ENTER_ARG(ip));
        return;
    }

    prog = thread->regs[ACE_ENTER_ARG(ip)];

again:
    if (!IS_PTR(prog)) {
        ace_return(thread, ip->pos, prog);
        return;
    }
    if (gsisheap_block(BLOCK_CONTAINING(prog))) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)prog;
        gsheap_lock(hp);
        st = gsheapstate(ip->pos, hp);
        switch (st) {
            case gstythunk:
                if ((uchar*)thread->stacktop - (uchar*)thread->stacklimit >= 0x100 * sizeof(gsvalue)) {
                    ace_thread_enter_closure(ip->pos, thread, hp, stats);
                    return;
                } else {
                    gsstatprint("%P: %P: Allocating a new thread to avoid stack overflow\n", thread->cureval->cont.pos, ip->pos);
                    stats->num_blocks_on_new_stack++;
                    st = ace_start_evaluation(ip->pos, hp);
                    break;
                }
            case gstystack:
                stats->num_blocks_on_existing_thread++;
            case gstywhnf:
            case gstyindir:
            case gstyenosys:
                gsheap_unlock(hp);
                break;
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter heap items of state %d", st);
                gsheap_unlock(hp);
                return;
        }
    } else {
        st = GS_SLOW_EVALUATE(ip->pos, prog);
    }

    switch (st) {
        case gstystack:
        case gstyblocked:
            gsstatprint("%P: Blocking on a %s\n", ip->pos, CLASS_OF_BLOCK_CONTAINING(prog)->description);
            thread->state = ace_thread_blocked;
            thread->st.blocked.on = prog;
            thread->st.blocked.at = ip->pos;
            return;
        case gstyindir:
            prog = GS_REMOVE_INDIRECTION(ip->pos, prog);
            goto again;
        case gstywhnf:
            ace_return(thread, ip->pos, prog);
            return;
        case gstyerr:
            ace_error_thread(thread, (struct gserror*)prog);
            return;
        case gstyimplerr:
            ace_failure_thread(thread, (struct gsimplementation_failure *)prog);
            return;
        case gstyenosys:
        case gstyeoothreads:
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "%r");
            return;
        default:
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter (st = %d)", st);
            return;
    }
}

static
void
ace_instr_yield(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    if (ACE_YIELD_ARG(ip) >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ACE_YIELD_ARG(ip));
        return;
    }

    ace_return(thread, ip->pos, thread->regs[ACE_YIELD_ARG(ip)]);
}

static
void
ace_instr_ubprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsregistered_primset *prims;
    gsvalue args[MAX_NUM_REGISTERS];
    int i;

    ip = thread->st.running.ip;

    for (i = 0; i < ACE_UBPRIM_NARGS(ip); i++)
        args[i] = thread->regs[ACE_UBPRIM_ARG(ip, i)]
    ;
    prims = gsprims_lookup_prim_set_by_index(ACE_UBPRIM_PRIMSET_INDEX(ip));
    prims->ubexec_table[ACE_UBPRIM_INDEX(ip)](thread, ip->pos, ACE_UBPRIM_NARGS(ip), args);
}

static
void
ace_instr_lprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsregistered_primset *prims;
    gsvalue args[MAX_NUM_REGISTERS];
    int i;

    ip = thread->st.running.ip;

    for (i = 0; i < ACE_LPRIM_NARGS(ip); i++)
        args[i] = thread->regs[ACE_LPRIM_ARG(ip, i)]
    ;
    prims = gsprims_lookup_prim_set_by_index(ACE_LPRIM_PRIMSET_INDEX(ip));
    prims->lexec_table[ACE_LPRIM_INDEX(ip)](thread, ip->pos, ACE_LPRIM_NARGS(ip), args);
}

static void *ace_set_registers_from_bco(struct ace_thread *, struct gsbco *);

int
gsprim_return(struct ace_thread *thread, struct gspos pos, gsvalue v)
{
    ace_return(thread, pos, v);
    return 1;
}

int
gsprim_return_ubsum(struct ace_thread *thread, struct gspos pos, int constr, int nargs, ...)
{
    struct ace_cont *cont;
    struct ace_stack_ubanalyze_cont *ubanalyze;
    struct gsbc *ip;
    va_list arg;
    int i;

    cont = thread->stacktop;
    ubanalyze = (struct ace_stack_ubanalyze_cont *)cont;

    if (cont->node != ace_stack_ubanalyze_cont) {
        ace_poison_thread(thread, pos, "gsubprim_return: top of stack is a %d not a ubanalyze", cont->node);
        return 0;
    }

    ip = ace_set_registers_from_bco(thread, ACE_STACK_UBANALYZE_CONT(*ubanalyze, constr));
    if (!ip) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "Too many registers");
        return 0;
    }
    for (i = 0; i < ubanalyze->numfvs; i++)
        thread->regs[thread->nregs++] = ACE_STACK_UBANALYZE_FV(*ubanalyze, i)
    ;
    va_start(arg, nargs);
    for (i = 0; i < nargs; i++)
        thread->regs[thread->nregs++] = va_arg(arg, gsvalue)
    ;
    va_end(arg);

    thread->st.running.bco = ACE_STACK_UBANALYZE_CONT(*ubanalyze, constr);
    thread->st.running.ip = ip;
    thread->stacktop = ACE_STACK_UBANALYZE_BOTTOM(ubanalyze);
    return 1;
}

int
gsprim_unimpl(struct ace_thread *thread, char *srcfile, int srclineno, struct gspos pos, char *msg, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, msg);
    vseprint(buf, buf + sizeof(buf), msg, arg);
    va_end(arg);

    ace_thread_unimpl(thread, srcfile, srclineno, pos, "%s", buf);
    return 0;
}

int
gsprim_error(struct ace_thread *thread, struct gserror *error)
{
    ace_error_thread(thread, error);
    return 0;
}

int
gsprim_block(struct ace_thread *thread, struct gspos pos, struct gslprim_blocking *blocking)
{
    thread->state = ace_thread_lprim_blocked;
    thread->st.lprim_blocked.at = pos;
    thread->st.lprim_blocked.on = blocking;
    return 0;
}

static void ace_remove_thread(struct ace_thread *);

static
void
ace_error_thread(struct ace_thread *thread, struct gserror *err)
{
    struct gsheap_item *hp;
    struct gsbc_cont_update *update;
    struct gsindirection *in;

    if (thread->state == ace_thread_finished) return;

    for (update = thread->cureval; update; update = update->next) {
        hp = update->dest;

        lock(&hp->lock);
        hp->type = gsindirection;
        in = (struct gsindirection *)hp;
        in->dest = (gsvalue)err;
        unlock(&hp->lock);
    }

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

void
ace_failure_thread(struct ace_thread *thread, struct gsimplementation_failure *err)
{
    struct gsbc_cont_update *update;
    struct gsheap_item *hp;
    struct gsindirection *in;

    if (thread->state == ace_thread_finished) return;

    for (update = thread->cureval; update; update = update->next) {
        hp = update->dest;

        lock(&hp->lock);
        hp->type = gsindirection;
        in = (struct gsindirection *)hp;
        in->dest = (gsvalue)err;
        unlock(&hp->lock);
    }

    ace_remove_thread(thread);
}

static void *ace_set_registers_from_closure(struct ace_thread *, struct gsclosure *);

static int ace_return_to_update(struct ace_thread *, struct ace_cont *, gsvalue);
static void ace_return_to_app(struct ace_thread *, struct ace_cont *, gsvalue);
static void ace_return_to_force(struct ace_thread *, struct ace_cont *, gsvalue);
static void ace_return_to_strict(struct ace_thread *, struct ace_cont *, gsvalue);

static
void
ace_return(struct ace_thread *thread, struct gspos srcpos, gsvalue v)
{
    struct ace_cont *cont;

again:
    cont = ace_stack_top(thread);
    switch (cont->node) {
        case gsbc_cont_update:
            if (ace_return_to_update(thread, cont, v)) {
                goto again;
            } else {
                return;
            }
        case gsbc_cont_app:
            ace_return_to_app(thread, cont, v);
            return;
        case gsbc_cont_force:
            ace_return_to_force(thread, cont, v);
            return;
        case gsbc_cont_strict:
            ace_return_to_strict(thread, cont, v);
            return;
        default:
            ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Return to %d continuations", cont->node);
            return;
    }
}

int
ace_return_to_update(struct ace_thread *thread, struct ace_cont *cont, gsvalue v)
{
    struct gsbc_cont_update *update;
    struct gsheap_item *hp;
    struct gsindirection *in;

    update = (struct gsbc_cont_update *)cont;
    hp = update->dest;

    lock(&hp->lock);
    hp->type = gsindirection;
    in = (struct gsindirection *)hp;
    in->dest = v;
    unlock(&hp->lock);

    if (update->next) {
        ace_pop_update(thread);
        return 1;
    } else {
        ace_remove_thread(thread);
        return 0;
    }
}

static
void
ace_return_to_app(struct ace_thread *thread, struct ace_cont *cont, gsvalue v)
{
    int i;
    struct gs_blockdesc *block;
    struct gsheap_item *fun;
    struct gsclosure *cl;
    struct gsbc_cont_app *app;
    int needed_args;

    app = (struct gsbc_cont_app *)cont;

    block = BLOCK_CONTAINING(v);

    if (!gsisheap_block(block)) {
        ace_poison_thread(thread, cont->pos, "Applied function is a %s not a closure", block->class->description);
        return;
    }

    fun = (struct gsheap_item *)v;

    if (fun->type != gsclosure) {
        ace_poison_thread(thread, cont->pos, "Applied function %P is not a closure", fun->pos);
        return;
    }

    cl = (struct gsclosure *)fun;
    needed_args = cl->cl.code->numargs - (cl->cl.numfvs - cl->cl.code->numfvs);

    if (app->numargs < needed_args) {
        struct gsheap_item *res;
        struct gsclosure *clres;

        res = gsreserveheap(sizeof (struct gsclosure) + (cl->cl.numfvs + app->numargs) * sizeof(gsvalue));
        clres = (struct gsclosure *)res;

        res->pos = cont->pos;
        memset(&res->lock, 0, sizeof(res->lock));
        res->type = gsclosure;
        clres->cl.code = cl->cl.code;
        clres->cl.numfvs = cl->cl.numfvs + app->numargs;
        for (i = 0; i < cl->cl.numfvs; i++)
            clres->cl.fvs[i] = cl->cl.fvs[i]
        ;
        for (i = cl->cl.numfvs; i < cl->cl.numfvs + app->numargs; i++)
            clres->cl.fvs[i] = app->arguments[i - cl->cl.numfvs]
        ;
        thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);

        ace_return(thread, cont->pos, (gsvalue)res);
        return;
    } else {
        int i;
        void *ip;

        switch (cl->cl.code->tag) {
            case gsbc_expr: {
                void *bot_of_app_cont, *newstacktop;

                ip = ace_set_registers_from_closure(thread, cl);
                if (!ip) {
                    ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                    return;
                }

                for (i = 0; i < needed_args; i++) {
                    if (thread->nregs >= MAX_NUM_REGISTERS) {
                        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
                        return;
                    }
                    thread->regs[thread->nregs] = app->arguments[i];
                    thread->nregs++;
                }

                bot_of_app_cont = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);

                if (i < app->numargs) {
                    struct gsbc_cont_app *newapp;

                    newstacktop = (uchar*)bot_of_app_cont - sizeof(struct gsbc_cont_app) - (app->numargs - i) * sizeof(gsvalue);
                    newapp = (struct gsbc_cont_app *)newstacktop;
                    /* §ccode{newapp} has to be initialized from highest address to lowest */
                    newapp->numargs = app->numargs - i;
                    newapp->cont.pos = app->cont.pos;
                    newapp->cont.node = gsbc_cont_app;
                } else {
                    newstacktop = bot_of_app_cont;
                }

                thread->stacktop = newstacktop;
                thread->state = ace_thread_running;
                thread->st.running.bco = cl->cl.code;
                thread->st.running.ip = (struct gsbc *)ip;
                break;
            }
            case gsbc_impprog: {
                struct gsheap_item *hpres;
                struct gsclosure *res;

                hpres = gsreserveheap(sizeof(*res) + (cl->cl.numfvs + app->numargs) * sizeof(gsvalue));
                res = (struct gsclosure *)hpres;

                memset(&hpres->lock, 0, sizeof(hpres->lock));
                hpres->pos = cont->pos;
                hpres->type = gsclosure;
                res->cl.code = cl->cl.code;
                res->cl.numfvs = cl->cl.numfvs + app->numargs;
                for (i = 0; i < cl->cl.numfvs; i++)
                    res->cl.fvs[i] = cl->cl.fvs[i]
                ;
                for (i = 0; i < app->numargs; i++)
                    res->cl.fvs[cl->cl.numfvs + i] = app->arguments[i]
                ;

                thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);
                return ace_return(thread, cont->pos, (gsvalue)res);
            }
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, fun->pos, "Apply code blocks of type %d", cl->cl.code->tag);
                return;
        }
    }
}

static
void
ace_return_to_force(struct ace_thread *thread, struct ace_cont *cont, gsvalue v)
{
    struct gsbc_cont_force *force;
    void *ip;
    int i;

    force = (struct gsbc_cont_force *)cont;

    ip = ace_set_registers_from_bco(thread, force->code);
    if (!ip) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return;
    }

    for (i = 0; i < force->numfvs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
            return;
        }
        thread->regs[thread->nregs++] = force->fvs[i];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs++] = v;

    thread->state = ace_thread_running;
    thread->st.running.bco = force->code;
    thread->st.running.ip = (struct gsbc *)ip;
    thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_force) + force->numfvs * sizeof(gsvalue);
}

static
void
ace_return_to_strict(struct ace_thread *thread, struct ace_cont *cont, gsvalue v)
{
    struct gsbc_cont_strict *strict;
    void *ip;
    int i;

    strict = (struct gsbc_cont_strict *)cont;

    ip = ace_set_registers_from_bco(thread, strict->code);
    if (!ip) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return;
    }

    for (i = 0; i < strict->numfvs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
            return;
        }
        thread->regs[thread->nregs++] = strict->fvs[i];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, cont->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs++] = v;

    thread->state = ace_thread_running;
    thread->st.running.bco = strict->code;
    thread->st.running.ip = (struct gsbc *)ip;
    thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_strict) + strict->numfvs * sizeof(gsvalue);
}

static
void
ace_remove_thread(struct ace_thread *thread)
{
    if (thread->state == ace_thread_finished) return;
    thread->state = ace_thread_finished;
    lock(&ace_thread_queue->lock);
    if (ace_thread_queue->num_active_threads < thread->tid) {
        gswarning("%s:%d: Can't shutdown thread #%d properly; only have %d threads total!", __FILE__, __LINE__, thread->tid, ace_thread_queue->num_active_threads);
        unlock(&ace_thread_queue->lock);
        return;
    }
    ace_thread_queue->num_active_threads--;
    if (ace_thread_queue->num_active_threads < 0) gswarning("%s:%d: Got %d threads!", __FILE__, __LINE__, ace_thread_queue->num_active_threads);
    if (ace_thread_queue->num_active_threads > thread->tid) {
        int last_tid, cur_tid;

        last_tid = ace_thread_queue->num_active_threads;
        cur_tid = thread->tid;
        ace_thread_queue->threads[cur_tid] = ace_thread_queue->threads[last_tid];
        ace_thread_queue->threads[last_tid] = 0;
        ace_thread_queue->threads[cur_tid]->tid = cur_tid;
    } else {
        ace_thread_queue->threads[thread->tid] = 0;
    }
    unlock(&ace_thread_queue->lock);
}

void
ace_up()
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

/* ↓ Used to start evaluation from outside ACE (so we allocate a new thread).
   Also used to start evaluation from §ags{.enter} instructions, when we decide there isn't enough room on the stack for a new eval.
*/
gstypecode
ace_start_evaluation(struct gspos pos, struct gsheap_item *hp)
{
    struct ace_thread *thread;

    thread = ace_thread_alloc();

    if (ace_thread_enter_closure(pos, thread, hp, 0) < 0) {
        unlock(&thread->lock);
        return gstyindir;
    }

    unlock(&thread->lock);

    lock(&ace_thread_queue->lock);
    if (ace_thread_queue->num_active_threads >= NUM_ACE_THREADS) {
        unlock(&ace_thread_queue->lock);
        werrstr("oothreads");
        return gstyeoothreads;
    } else {
        thread->tid = ace_thread_queue->num_active_threads;
        ace_thread_queue->threads[ace_thread_queue->num_active_threads++] = thread;
        ace_thread_queue->numthreads++;
        unlock(&ace_thread_queue->lock);
        return gstystack;
    }
}

/* ↓ Used to add a closure (necessarily a §ccode{struct gsheap_item *}) to a thread.
   Assume there is enough room for an update frame: it's the caller's responsibility to create a new thread when we're low on stack space.
*/
int
ace_thread_enter_closure(struct gspos pos, struct ace_thread *thread, struct gsheap_item *hp, struct ace_thread_pool_stats *stats)
{
    struct gsbc_cont_update *updatecont;

    if (!(updatecont = ace_push_update(pos, thread, hp))) return -1;

    switch (hp->type) {
        case gsclosure: {
            struct gsclosure *cl;
            struct gsbc *instr;

            cl = (struct gsclosure *)hp;

            switch (cl->cl.code->tag) {
                case gsbc_expr: {
                    instr = (struct gsbc *)ace_set_registers_from_closure(thread, cl);

                    if (!instr) {
                        ace_failure_thread(thread, gsunimpl(__FILE__, __LINE__, cl->cl.code->pos, "Too many registers"));
                        return -1;
                    }

                    thread->state = ace_thread_running;
                    thread->st.running.bco = cl->cl.code;
                    thread->st.running.ip = instr;

                    cl->hp.type = gseval;
                    cl->update = updatecont;
                    gsheap_unlock(hp);

                    break;
                }
                default:
                    gsheap_unlock(hp);
                    ace_failure_thread(thread, gsunimpl(__FILE__, __LINE__, pos, "ace_start_evaluation(%d)", cl->cl.code->tag));
                    return -1;
            }

            return 0;
        }
        case gsapplication: {
            struct gsapplication *app;
            gsvalue fun;
            struct gspos pos;

            app = (struct gsapplication *)hp;

            fun = app->app.fun;
            pos = hp->pos;
            if (!ace_push_appv(pos, thread, app->app.numargs, app->app.arguments)) return -1;

            app->hp.type = gseval;
            app->update = updatecont;
            gsheap_unlock(hp);

            thread->state = ace_thread_blocked;
            thread->st.blocked.on = fun;
            thread->st.blocked.at = pos;

            if (stats) stats->num_blocks_on_function++;

            return 0;
        }
        default:
            gsheap_unlock(hp);
            ace_failure_thread(thread, gsunimpl(__FILE__, __LINE__, pos, "ace_start_evaluation(type = %d)", hp->type));
            return -1;
    }
}

static
void *
ace_set_registers_from_closure(struct ace_thread *thread, struct gsclosure *cl)
{
    void *ip;
    int i;

    ip = ace_set_registers_from_bco(thread, cl->cl.code);
    if (!ip) return 0;
    for (i = 0; i < cl->cl.numfvs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS)
            return 0
        ;
        thread->regs[thread->nregs] = cl->cl.fvs[i];
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

#define ACE_STACK_ARENA_SIZE (sizeof(gsvalue) * 0x400)

static struct gs_sys_aligned_block_suballoc_info ace_thread_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "ACE Thread",
    },
    /* align = */ ACE_STACK_ARENA_SIZE,
};

struct ace_thread *
ace_thread_alloc()
{
    struct ace_thread *thread;
    void *stackbase, *stackbot;

    gs_sys_aligned_block_suballoc(&ace_thread_info, &stackbase, &stackbot);

    thread = (struct ace_thread *)stackbase;

    memset(&thread->lock, 0, sizeof(thread->lock));
    lock(&thread->lock);

    thread->stackbot = stackbot;
    thread->stacktop = stackbot;
    thread->stacklimit = (uchar*)thread + sizeof(*thread);
    thread->cureval = 0;

    return thread;
}

static struct ace_thread *ace_thread_gccopy(struct gsstringbuilder *, struct ace_thread *);
static int ace_thread_gcevacuate(struct gsstringbuilder *, struct ace_thread *);

int
ace_eval_gc_trace(struct gsstringbuilder *err, struct gsbc_cont_update **pupdate)
{
    struct ace_thread *thread, *newthread;
    struct gsbc_cont_update *update, *newupdate;

    update = *pupdate;
    thread = (struct ace_thread *)((uintptr)update - (uintptr)update % ACE_STACK_ARENA_SIZE);
    if (!((uintptr)thread % BLOCK_SIZE)) thread = START_OF_BLOCK((struct gs_blockdesc *)thread);

    if (!gs_sys_block_in_gc_from_space(thread)) {
        gsstring_builder_print(err, UNIMPL("ace_eval_gc_trace: set newthread for to-space thread"));
        return -1;
    } else if (thread->state == ace_thread_gcforward) {
        newthread = thread->st.forward.dest;
    } else {
        if (!(newthread = ace_thread_gccopy(err, thread))) return -1;

        thread->state = ace_thread_gcforward;
        thread->st.forward.dest = newthread;

        if (ace_thread_gcevacuate(err, newthread) < 0) return -1;
    }

    /* ↓ §ccode{(uchar*)newthread->stackbot - (uchar*)newupdate = (uchar*)thread->stackbot - (uchar*)update} */
    *pupdate = newupdate = (struct gsbc_cont_update *)((uchar*)newthread->stackbot - ((uchar*)thread->stackbot - (uchar*)update));

    if (ace_stack_gcevacuate(err, newthread, newupdate) < 0) return -1;

    return 0;
}

struct ace_thread *
ace_thread_gccopy(struct gsstringbuilder *err, struct ace_thread *thread)
{
    struct ace_thread *newthread;
    void *stackbase, *stackbot, *stacklimit;
    struct gsbc_cont_update *update;
    ulong stacksize;

    gs_sys_aligned_block_suballoc(&ace_thread_info, &stackbase, &stackbot);
    newthread = stackbase;
    stacklimit = (uchar*)newthread + sizeof(*newthread);

    memcpy(newthread, thread, sizeof(*thread));

    stacksize = (uchar*)thread->stackbot - (uchar*)thread->stacktop;
    if (stacksize > (uchar*)stackbot - (uchar*)stacklimit) {
        gsstring_builder_print(err, UNIMPL("ace_eval_gc_trace: no room for new stack"));
        return 0;
    }

    newthread->stackbot = stackbot;
    newthread->stacktop = (uchar*)stackbot - stacksize;
    newthread->stacklimit = stacklimit;
    memcpy(newthread->stacktop, thread->stacktop, stacksize);
    newthread->gc_evacuated_stackbot = newthread->stacktop;

    /* ↓ §ccode{(uchar*)newthread->stackbot - (uchar*)thread->cureval = (uchar*)thread->stackbot - (uchar*)thread->cureval} */
    newthread->cureval = (struct gsbc_cont_update *)((uchar*)newthread->stackbot - ((uchar*)thread->stackbot - (uchar*)thread->cureval));
    for (update = newthread->cureval; update; update = update->next) {
        if (update->next)
            update->next = (struct gsbc_cont_update *)((uchar*)newthread->stackbot - ((uchar*)thread->stackbot - (uchar*)update->next))
        ;
    }

    memset(&newthread->lock, 0, sizeof(newthread->lock));

    return newthread;
}

int
ace_thread_gcevacuate(struct gsstringbuilder *err, struct ace_thread *thread)
{
    gsvalue gctemp;
    int i;

    switch (thread->state) {
        case ace_thread_running: {
            void *oldbco, *newip;

            oldbco = thread->st.running.bco;
            if (thread->st.running.bco && gs_sys_block_in_gc_from_space(thread->st.running.bco) && gs_gc_trace_bco(err, &thread->st.running.bco) < 0) return -1;
            newip = (uchar*)thread->st.running.bco + ((uchar*)thread->st.running.ip - (uchar*)oldbco);
            thread->st.running.ip = (struct gsbc *)newip;

            for (i = 0; i < thread->nsubexprs; i++)
                if (gs_gc_trace_bco(err, &thread->subexprs[i]) < 0) return -1
            ;

            for (i = 0; i < thread->nregs; i++)
                if (GS_GC_TRACE(err, &thread->regs[i]) < 0) return -1
            ;

            break;
        }
        case ace_thread_blocked:
            if (GS_GC_TRACE(err, &thread->st.blocked.on) < 0) return -1;
            if (gs_gc_trace_pos(err, &thread->st.blocked.at) < 0) return -1;
            break;
        case ace_thread_lprim_blocked:
            if (thread->st.lprim_blocked.on && gs_sys_block_in_gc_from_space(thread->st.lprim_blocked.on) && gslprim_blocking_trace(err, &thread->st.lprim_blocked.on) < 0) return -1;
            if (gs_gc_trace_pos(err, &thread->st.lprim_blocked.at) < 0) return -1;

            break;
        default:
            gsstring_builder_print(err, UNIMPL("ace_eval_gc_trace: evacuate st: state = %d"), thread->state);
            return -1;
    }

    return 0;
}

/* §section §ags{.lprim} Blocking Allocation */

static struct gs_sys_global_block_suballoc_info gslprim_blocking_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ ".prim blocking information",
    },
};

void *
gslprim_blocking_alloc(long sz, gslprim_resumption_handler *resume, gslprim_gccopy_handler *gccopy, gslprim_gcevacuate_handler *gcevacuate)
{
    struct gslprim_blocking *res;

    res = gs_sys_global_block_suballoc(&gslprim_blocking_info, sz);

    res->resume = resume;
    res->gccopy = gccopy;
    res->gcevacuate = gcevacuate;
    res->forward = 0;

    return res;
}

int
gslprim_blocking_trace(struct gsstringbuilder *err, struct gslprim_blocking **pblocking)
{
    struct gslprim_blocking *blocking, *newblocking;

    blocking = *pblocking;

    if (blocking->forward) {
        gsstring_builder_print(err, UNIMPL("gslprim_blocking_trace: check for forward"));
        return -1;
    }

    if (!(newblocking = blocking->gccopy(err, blocking))) return -1;

    *pblocking = blocking->forward = newblocking;

    if (newblocking->gcevacuate(err, newblocking) < 0) return -1;

    return 0;
}
