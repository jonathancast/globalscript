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

    ace_up();

    if ((ace_pool = gscreate_thread_pool(ace_thread_pool_main, &ace_thread_pool_args, sizeof(ace_thread_pool_args))) < 0)
        gsfatal("Couldn't create ACE thread pool: %r")
    ;

    return 0;
}

static void ace_return(struct ace_thread *, struct gspos, gsvalue);
static void ace_error_thread(struct ace_thread *, struct gserror *);
static void ace_failure_thread(struct ace_thread *, struct gsimplementation_failure *);

static void ace_extract_efv(struct ace_thread *);
static void ace_alloc_thunk(struct ace_thread *);
static void ace_prim(struct ace_thread *);
static void ace_alloc_constr(struct ace_thread *);
static void ace_alloc_record(struct ace_thread *);
static void ace_extract_field(struct ace_thread *);
static void ace_alloc_lfield(struct ace_thread *);
static void ace_alloc_undef(struct ace_thread *);
static void ace_alloc_apply(struct ace_thread *);
static void ace_alloc_unknown_eprim(struct ace_thread *);
static void ace_alloc_eprim(struct ace_thread *);
static void ace_push_app(struct ace_thread *);
static void ace_push_force(struct ace_thread *);
static void ace_push_strict(struct ace_thread *);
static void ace_push_ubanalyze(struct ace_thread *);
static void ace_perform_analyze(struct ace_thread *);
static void ace_perform_danalyze(struct ace_thread *);
static void ace_return_undef(struct ace_thread *);
static void ace_enter(struct ace_thread *);
static void ace_yield(struct ace_thread *);
static void ace_ubprim(struct ace_thread *);
static void ace_lprim(struct ace_thread *);

static
void
ace_thread_pool_main(void *p)
{
    int tid, last_tid;
    int nwork;
    vlong outer_loops, numthreads_total, num_instrs, num_blocked, num_blocked_threads;
    vlong start_time, end_time, finding_thread_time, finding_thread_start_time;

    outer_loops = numthreads_total = num_instrs = num_blocked = num_blocked_threads = 0;
    start_time = nsec();
    finding_thread_time = 0;
    tid = 0;
    for (;;) {
        struct ace_thread *thread;
        outer_loops++;

        last_tid = tid;
        finding_thread_start_time = nsec();
        do {
            tid = (tid + 1) % NUM_ACE_THREADS;

            if (0) if (gsflag_stat_collection) gswarning("%s:%d: Locking for tid %d", __FILE__, __LINE__, tid);
            lock(&ace_thread_queue->lock);
                if (ace_thread_queue->refcount <= 0) {
                    unlock(&ace_thread_queue->lock);
                    goto no_clients;
                }
                thread = ace_thread_queue->threads[tid];
            unlock(&ace_thread_queue->lock);
            if (0) if (gsflag_stat_collection) gswarning("%s:%d: Un-locking for tid %d", __FILE__, __LINE__, tid);

            if (thread) {
                numthreads_total++;
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

                        num_blocked++;
                    again:
                        st = GS_SLOW_EVALUATE(thread->st.blocked.on);

                        switch (st) {
                            case gstystack:
                                num_blocked_threads++;
                            case gstyblocked:
                                break;
                            case gstyindir:
                                thread->st.blocked.on = GS_REMOVE_INDIRECTION(thread->st.blocked.on);
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
            }
            if (!thread && tid == last_tid)
                sleep(1)
            ;
        } while (!thread);
        if (gsflag_stat_collection) finding_thread_time += nsec() - finding_thread_start_time;

        nwork = 0;
        while (thread->state == ace_thread_running && nwork++ < 0x100) {
            num_instrs++;
            switch (thread->st.running.ip->instr) {
                case gsbc_op_efv:
                    ace_extract_efv(thread);
                    break;
                case gsbc_op_alloc:
                    ace_alloc_thunk(thread);
                    break;
                case gsbc_op_prim:
                    ace_prim(thread);
                    break;
                case gsbc_op_constr:
                    ace_alloc_constr(thread);
                    break;
                case gsbc_op_record:
                    ace_alloc_record(thread);
                    break;
                case gsbc_op_field:
                    ace_extract_field(thread);
                    break;
                case gsbc_op_lfield:
                    ace_alloc_lfield(thread);
                    break;
                case gsbc_op_undefined:
                    ace_alloc_undef(thread);
                    break;
                case gsbc_op_apply:
                    ace_alloc_apply(thread);
                    break;
                case gsbc_op_unknown_eprim:
                    ace_alloc_unknown_eprim(thread);
                    break;
                case gsbc_op_eprim:
                    ace_alloc_eprim(thread);
                    break;
                case gsbc_op_app:
                    ace_push_app(thread);
                    break;
                case gsbc_op_force:
                    ace_push_force(thread);
                    break;
                case gsbc_op_strict:
                    ace_push_strict(thread);
                    break;
                case gsbc_op_ubanalzye:
                    ace_push_ubanalyze(thread);
                    break;
                case gsbc_op_analyze:
                    ace_perform_analyze(thread);
                    break;
                case gsbc_op_danalyze:
                    ace_perform_danalyze(thread);
                    break;
                case gsbc_op_undef:
                    ace_return_undef(thread);
                    break;
                case gsbc_op_enter:
                    ace_enter(thread);
                    break;
                case gsbc_op_yield:
                    ace_yield(thread);
                    break;
                case gsbc_op_ubprim:
                    ace_ubprim(thread);
                    break;
                case gsbc_op_lprim:
                    ace_lprim(thread);
                    break;
                default:
                    ace_thread_unimpl(thread, __FILE__, __LINE__, thread->st.running.ip->pos, "run instruction %d", thread->st.running.ip->instr);
                    break;
            }
        }

        unlock(&thread->lock);
    }
no_clients:
    end_time = nsec();

    if (gsflag_stat_collection) {
        fprint(2, "# ACE threads: %d\n", ace_thread_queue->numthreads);
        fprint(2, "Avg # instructions / ACE thread: %d\n", num_instrs / ace_thread_queue->numthreads);
        fprint(2, "# ACE outer loops: %lld\n", outer_loops);
        fprint(2, "Avg # ACE threads: %02g\n", (double)numthreads_total / outer_loops);
        fprint(2, "ACE threads: %2.2g%% instructions, %2.2g%% blocked, %2.2g%% blocked on threads\n", ((double)num_instrs / numthreads_total) * 100, ((double)num_blocked / numthreads_total) * 100, ((double)num_blocked_threads / numthreads_total) * 100);
        fprint(2, "ACE Run time: %llds %lldms\n", (end_time - start_time) / 1000 / 1000 / 1000, ((end_time - start_time) / 1000 / 1000) % 1000);
        fprint(2, "ACE Finding thread time: %llds %lldms\n", finding_thread_time / 1000 / 1000 / 1000, (finding_thread_time / 1000 / 1000) % 1000);
        fprint(2, "Avg unit of work: %gμs\n", (double)(end_time - start_time) / numthreads_total / 1000);
    }
}

static
void
ace_extract_efv(struct ace_thread *thread)
{
    struct gsbc *ip;
    gstypecode st;
    int nreg;

    ip = thread->st.running.ip;

    nreg = ACE_EFV_REGNUM(ip);

    for (;;) {
        st = GS_SLOW_EVALUATE(thread->regs[nreg]);

        switch (st) {
            case gstywhnf:
                goto extracted_value;
            case gstyindir:
                thread->regs[nreg] = GS_REMOVE_INDIRECTION(thread->regs[nreg]);
                break;
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "ace_extract_efv: st = %d", st);
                return;
        }
    }

extracted_value:

    thread->st.running.ip = ACE_EFV_SKIP(ip);
    return;
}

static
void
ace_alloc_thunk(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsheap_item *hp;
    struct gsclosure *cl;
    int i;

    ip = thread->st.running.ip;

    hp = gsreserveheap(sizeof(struct gsclosure) + ip->args[1] * sizeof(gsvalue));
    cl = (struct gsclosure *)hp;

    memset(&hp->lock, 0, sizeof(hp->lock));
    hp->pos = ip->pos;
    hp->type = gsclosure;
    if (ip->args[0] > thread->nsubexprs) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".alloc subexpr out of range");
        return;
    }
    cl->code = thread->subexprs[ip->args[0]];
    cl->numfvs = ip->args[1];
    for (i = 0; i < ip->args[1]; i++) {
        if (ip->args[2 + i] > thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".alloc free variable out of range");
            return;
        }
        cl->fvs[i] = thread->regs[ip->args[2 + i]];
    }
    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Register overflow in .alloc");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)hp;
    thread->nregs++;
    thread->st.running.ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);
    return;
}

static
void
ace_prim(struct ace_thread *thread)
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
ace_alloc_constr(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr_args *constr;
    int i;

    ip = thread->st.running.ip;

    constr = gsreserveconstrs(sizeof(*constr) + ACE_CONSTR_NUMARGS(ip) * sizeof(gsvalue));
    constr->c.pos = ip->pos;
    constr->c.type = gsconstr_args;
    constr->constrnum = ACE_CONSTR_CONSTRNUM(ip);
    constr->numargs = ACE_CONSTR_NUMARGS(ip);
    for (i = 0; i < ACE_CONSTR_NUMARGS(ip); i++)
        constr->arguments[i] = thread->regs[ACE_CONSTR_ARG(ip, i)]
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
ace_alloc_record(struct ace_thread *thread)
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
    fields->numfields = ip->args[0];
    for (j = 0; j < ip->args[0]; j++)
        fields->fields[j] = thread->regs[ACE_RECORD_FIELD(ip, j)]
    ;

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)record;
    thread->nregs++;
    thread->st.running.ip = GS_NEXT_BYTECODE(ip, 1 + ip->args[0]);
    return;
}

static
void
ace_extract_field(struct ace_thread *thread)
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
ace_alloc_lfield(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    thread->regs[thread->nregs++] = gslfield(ip->pos, ACE_LFIELD_FIELD(ip), thread->regs[ACE_LFIELD_RECORD(ip)]);
    thread->st.running.ip = ACE_LFIELD_SKIP(ip);
    return;
}

static
void
ace_alloc_undef(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gserror *err;

    ip = thread->st.running.ip;

    err = gsreserveerrors(sizeof(*err));
    err->pos = ip->pos;
    err->type = gserror_undefined;

    thread->regs[thread->nregs] = (gsvalue)err;
    thread->nregs++;
    thread->st.running.ip = ACE_UNDEFINED_SKIP(ip);
    return;
}

static
void
ace_alloc_apply(struct ace_thread *thread)
{
    struct gsbc *ip;
    gsvalue fun;
    int i;

    ip = thread->st.running.ip;

    fun = thread->regs[ACE_APPLY_FUN(ip)];

    for (i = 0; i < ACE_APPLY_NUM_ARGS(ip); i++)
        fun = gsapply(ip->pos, fun, thread->regs[ACE_APPLY_ARG(ip, i)])
    ;

    thread->regs[thread->nregs] = fun;
    thread->nregs++;
    thread->st.running.ip = ACE_APPLY_SKIP(ip);
    return;
}

static
void
ace_alloc_unknown_eprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;

    ip = thread->st.running.ip;

    prim = gsreserveeprims(sizeof(*prim));
    prim->pos = ip->pos;
    prim->index = -1;

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
ace_alloc_eprim(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gseprim *prim;
    int j;

    ip = thread->st.running.ip;

    prim = gsreserveeprims(sizeof(*prim) + ip->args[1] * sizeof(gsvalue));
    prim->pos = ip->pos;
    prim->index = ip->args[0];
    for (j = 0; j < ip->args[1]; j++) {
        if (ip->args[2 + j] >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".eprim argument too large");
            return;
        }
        prim->arguments[j] = thread->regs[ip->args[2 + j]];
    }

    if (thread->nregs >= MAX_NUM_REGISTERS) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, "Too many registers");
        return;
    }
    thread->regs[thread->nregs] = (gsvalue)prim;
    thread->nregs++;
    thread->st.running.ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);

    return;
}

static struct gsbc_cont *ace_stack_alloc(struct ace_thread *, struct gspos, ulong);

static
void
ace_push_app(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
    struct gsbc_cont_app *app;
    int j;

    ip = thread->st.running.ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_app) + ip->args[0] * sizeof(gsvalue));
    app = (struct gsbc_cont_app *)cont;
    if (!cont) return;

    cont->node = gsbc_cont_app;
    cont->pos = ip->pos;
    app->numargs = ACE_APP_NUMARGS(ip);
    for (j = 0; j < ACE_APP_NUMARGS(ip); j++) {
        if (ACE_APP_ARG(ip, j) >= thread->nregs) {
            ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".app argument too large: arg #%d is %d (%d registers to this point)", j, ACE_APP_ARG(ip, j), thread->nregs);
            return;
        }
        app->arguments[j] = thread->regs[ACE_APP_ARG(ip, j)];
    }

    thread->st.running.ip = ACE_APP_SKIP(ip);
    return;
}

static
void
ace_push_force(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
    struct gsbc_cont_force *force;
    int i;

    ip = thread->st.running.ip;

    cont = ace_stack_alloc(thread, ip->pos, sizeof(struct gsbc_cont_force) + ip->args[1] * sizeof(gsvalue));
    force = (struct gsbc_cont_force *)cont;
    if (!cont) return;

    cont->node = gsbc_cont_force;
    cont->pos = ip->pos;
    force->code = thread->subexprs[ip->args[0]];
    force->numfvs = ip->args[1];
    for (i = 0; i < ip->args[1]; i++) {
        force->fvs[i] = thread->regs[ip->args[2+i]];
    }

    thread->st.running.ip = GS_NEXT_BYTECODE(ip, 2 + ip->args[1]);
    return;
}

static
void
ace_push_strict(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
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
ace_push_ubanalyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsbc_cont *cont;
    struct gsbc_cont_ubanalyze *ubanalyze;
    int i;

    ip = thread->st.running.ip;

    cont = ace_stack_alloc(thread, ip->pos, ACE_UBANALYZE_STACK_SIZE(ACE_UBANALYZE_NUMCONTS(ip), ACE_UBANALYZE_NUMFVS(ip)));
    ubanalyze = (struct gsbc_cont_ubanalyze *)cont;
    if (!cont) return;

    cont->node = gsbc_cont_ubanalyze;
    cont->pos = ip->pos;
    ubanalyze->numconts = ACE_UBANALYZE_NUMCONTS(ip);
    ubanalyze->conts = (struct gsbco **)((uchar*)ubanalyze + sizeof(struct gsbc_cont_ubanalyze));
    ubanalyze->numfvs = ACE_UBANALYZE_NUMFVS(ip);
    ubanalyze->fvs = (gsvalue*)((uchar*)ubanalyze->conts + ACE_UBANALYZE_NUMCONTS(ip) * sizeof(struct gsbco *));

    for (i = 0; i < ACE_UBANALYZE_NUMCONTS(ip); i++)
        ubanalyze->conts[i] = thread->subexprs[ACE_UBANALYZE_CONT(ip, i)]
    ;
    for (i = 0; i < ACE_UBANALYZE_NUMFVS(ip); i++)
        ubanalyze->fvs[i] = thread->regs[ACE_UBANALYZE_FV(ip, i)]
    ;

    thread->st.running.ip = ACE_UBANALYZE_SKIP(ip);
    return;
}

static void ace_poison_thread(struct ace_thread *, struct gspos, char *, ...);

static
void
ace_perform_analyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr_args *constr;
    struct gsbc **cases;

    int i;

    ip = thread->st.running.ip;

    constr = (struct gsconstr_args *)thread->regs[ACE_ANALYZE_SCRUTINEE(ip)];
    cases = ACE_ANALYZE_CASES(ip);

    thread->st.running.ip = ip = cases[constr->constrnum];
    for (i = 0; i < constr->numargs; i++) {
        if (thread->nregs >= MAX_NUM_REGISTERS) {
            ace_poison_thread(thread, ip->pos, "Register overflow");
            return;
        }
        thread->regs[thread->nregs++] = constr->arguments[i];
    }

    return;
}

static
void
ace_perform_danalyze(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gsconstr_args *constr;
    struct gsbc **cases;
    int casenum;

    int i;

    ip = thread->st.running.ip;

    constr = (struct gsconstr_args *)thread->regs[ACE_DANALYZE_SCRUTINEE(ip)];
    cases = ACE_DANALYZE_CASES(ip);

    casenum = 0;
    for (i = 0; i < ACE_DANALYZE_NUMCONSTRS(ip); i++) {
        if (ACE_DANALYZE_CONSTR(ip, i) == constr->constrnum) {
            casenum = 1 + i;
            break;
        }
    }

    thread->st.running.ip = ip = cases[casenum];
    if (casenum > 0) {
        for (i = 0; i < constr->numargs; i++) {
            if (thread->nregs >= MAX_NUM_REGISTERS) {
                ace_poison_thread(thread, ip->pos, "Register overflow");
                return;
            }
            thread->regs[thread->nregs++] = constr->arguments[i];
        }
    }

    return;
}

static
void
ace_return_undef(struct ace_thread *thread)
{
    struct gsbc *ip;
    struct gserror *err;

    ip = thread->st.running.ip;

    err = gsreserveerrors(sizeof(*err));
    err->pos = ip->pos;
    err->type = gserror_undefined;

    ace_error_thread(thread, err);
}

static
void
ace_enter(struct ace_thread *thread)
{
    struct gsbc *ip;
    gstypecode st;
    gsvalue prog;

    ip = thread->st.running.ip;

    if (ip->args[0] >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
        return;
    }

    prog = thread->regs[ip->args[0]];

    for (;;) {
        if (!IS_PTR(prog)) {
            ace_return(thread, ip->pos, prog);
            return;
        }
        if (gsisheap_block(BLOCK_CONTAINING(prog))) {
            struct gsheap_item *hp;

            hp = (struct gsheap_item *)prog;
            gsheap_lock(hp);
            st = gsheapstate(hp);
            switch (st) {
                case gstythunk:
                    st = ace_start_evaluation(prog);
                    gsheap_unlock(hp);
                    break;
                case gstystack:
                case gstywhnf:
                case gstyindir:
                    gsheap_unlock(hp);
                    break;
                default:
                    ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter heap items of state %d", st);
                    gsheap_unlock(hp);
                    return;
            }
        } else {
            st = GS_SLOW_EVALUATE(prog);
        }

        switch (st) {
            case gstystack:
            case gstyblocked:
                thread->state = ace_thread_blocked;
                thread->st.blocked.on = prog;
                thread->st.blocked.at = ip->pos;
                return;
            case gstyindir:
                prog = GS_REMOVE_INDIRECTION(prog);
                break;
            case gstywhnf:
                ace_return(thread, ip->pos, prog);
                return;
            case gstyerr:
                ace_error_thread(thread, (struct gserror*)prog);
                return;
            case gstyimplerr:
                ace_failure_thread(thread, (struct gsimplementation_failure *)prog);
                return;
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, ip->pos, ".enter (st = %d)", st);
                return;
        }
    }
}

static
void
ace_yield(struct ace_thread *thread)
{
    struct gsbc *ip;

    ip = thread->st.running.ip;

    if (ip->args[0] >= thread->nregs) {
        ace_poison_thread(thread, ip->pos, "Register #%d not allocated", (int)ip->args[0]);
        return;
    }

    ace_return(thread, ip->pos, thread->regs[ip->args[0]]);
}

static
void
ace_ubprim(struct ace_thread *thread)
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
ace_lprim(struct ace_thread *thread)
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
    struct gsbc_cont *cont;
    struct gsbc_cont_ubanalyze *ubanalyze;
    struct gsbc *ip;
    va_list arg;
    int i;

    cont = thread->stacktop;
    ubanalyze = (struct gsbc_cont_ubanalyze *)cont;

    if (cont->node != gsbc_cont_ubanalyze) {
        ace_poison_thread(thread, pos, "gsubprim_return: top of stack is a %d not a ubanalyze", cont->node);
        return 0;
    }

    ip = ace_set_registers_from_bco(thread, ubanalyze->conts[constr]);
    if (!ip) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "Too many registers");
        return 0;
    }
    for (i = 0; i < ubanalyze->numfvs; i++)
        thread->regs[thread->nregs++] = ubanalyze->fvs[i]
    ;
    va_start(arg, nargs);
    for (i = 0; i < nargs; i++)
        thread->regs[thread->nregs++] = va_arg(arg, gsvalue)
    ;
    va_end(arg);

    thread->st.running.ip = ip;
    thread->stacktop = (uchar*)thread->stacktop + ACE_UBANALYZE_STACK_SIZE(ubanalyze->numconts, ubanalyze->numfvs);
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

static void ace_return_to_app(struct ace_thread *, struct gsbc_cont *, gsvalue);
static void ace_return_to_force(struct ace_thread *, struct gsbc_cont *, gsvalue);
static void ace_return_to_strict(struct ace_thread *, struct gsbc_cont *, gsvalue);

static
void
ace_return(struct ace_thread *thread, struct gspos srcpos, gsvalue v)
{
    if ((uchar*)thread->stacktop >= (uchar*)thread->stackbot) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)thread->base;

        lock(&hp->lock);
        gsupdate_heap(hp, v);
        unlock(&hp->lock);

        ace_remove_thread(thread);
        return;
    } else {
        struct gsbc_cont *cont;

        cont = (struct gsbc_cont *)thread->stacktop;
        switch (cont->node) {
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
}

static
void
ace_return_to_app(struct ace_thread *thread, struct gsbc_cont *cont, gsvalue v)
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
    needed_args = cl->code->numargs - (cl->numfvs - cl->code->numfvs);

    if (app->numargs < needed_args) {
        struct gsheap_item *res;
        struct gsclosure *clres;

        res = gsreserveheap(sizeof (struct gsclosure) + (cl->numfvs + app->numargs) * sizeof(gsvalue));
        clres = (struct gsclosure *)res;

        res->pos = cont->pos;
        memset(&res->lock, 0, sizeof(res->lock));
        res->type = gsclosure;
        clres->code = cl->code;
        clres->numfvs = cl->numfvs + app->numargs;
        for (i = 0; i < cl->numfvs; i++)
            clres->fvs[i] = cl->fvs[i]
        ;
        for (i = cl->numfvs; i < cl->numfvs + app->numargs; i++)
            clres->fvs[i] = app->arguments[i - cl->numfvs]
        ;
        thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);

        ace_return(thread, cont->pos, (gsvalue)res);
        return;
    } else {
        int i;
        void *ip;

        switch (cl->code->tag) {
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
                thread->st.running.ip = (struct gsbc *)ip;
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

                thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_app) + app->numargs * sizeof(gsvalue);
                return ace_return(thread, cont->pos, (gsvalue)res);
            }
            default:
                ace_thread_unimpl(thread, __FILE__, __LINE__, fun->pos, "Apply code blocks of type %d", cl->code->tag);
                return;
        }
    }
}

static
void
ace_return_to_force(struct ace_thread *thread, struct gsbc_cont *cont, gsvalue v)
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
    thread->st.running.ip = (struct gsbc *)ip;
    thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_force) + force->numfvs * sizeof(gsvalue);
}

static
void
ace_return_to_strict(struct ace_thread *thread, struct gsbc_cont *cont, gsvalue v)
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
    thread->st.running.ip = (struct gsbc *)ip;
    thread->stacktop = (uchar*)cont + sizeof(struct gsbc_cont_strict) + strict->numfvs * sizeof(gsvalue);
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
    thread->state = ace_thread_finished;
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

                    thread->state = ace_thread_running;
                    thread->st.running.ip = instr;
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

            thread->state = ace_thread_blocked;
            thread->st.blocked.on = app->fun;
            thread->st.blocked.at = app->hp.pos;

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
            ace_thread_queue->numthreads++;
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
    /* gc_trace = */ gsunimplgc,
    /* description = */ "ACE Thread",
};
static void *ace_thread_nursury;
static int ace_thread_pre_gc_callback_registered;
static gs_sys_gc_pre_callback ace_thread_pre_gc_callback;

#define ACE_STACK_ARENA_SIZE (sizeof(gsvalue) * 0x400)

struct ace_thread *
ace_thread_alloc()
{
    struct gs_blockdesc *nursury_block;
    struct ace_thread *thread;
    void *stackbase, *stackbot;

    lock(&ace_thread_lock);
        if (ace_thread_nursury) {
            thread = ace_thread_nursury;
            nursury_block = BLOCK_CONTAINING(thread);
        } else {
            nursury_block = gs_sys_block_alloc(&ace_thread_descr);
            thread = START_OF_BLOCK(nursury_block);
            if (!ace_thread_pre_gc_callback_registered) {
                gs_sys_gc_pre_callback_register(ace_thread_pre_gc_callback);
                ace_thread_pre_gc_callback_registered = 1;
            }
        }
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

void
ace_thread_pre_gc_callback()
{
    ace_thread_nursury = 0;
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
gslprim_blocking_alloc(long sz, gslprim_resumption_handler *resume)
{
    struct gslprim_blocking *res;

    res = gs_sys_global_block_suballoc(&gslprim_blocking_info, sz);

    res->resume = resume;

    return res;
}
