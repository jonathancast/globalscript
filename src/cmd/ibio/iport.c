/* §source.file Input */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

/* §section Acceptors */

enum {
    ibio_acceptor_fail,
    ibio_acceptor_symbol_bind,
    ibio_acceptor_unit_plus,
};

void
ibio_check_acceptor_type(struct gspos pos, struct gsfile_symtable *symtable)
{
    gsinterned_string s, alpha;
    struct gskind *lifted, *kind;
    struct gstype *sv, *alphav;
    struct gstype *abstract, *applied_abstract, *expected;
    struct gsstringbuilder *err;

    err = gsreserve_string_builder();

    s = gsintern_string(gssymtypelable, "s");
    alpha = gsintern_string(gssymtypelable, "α");
    lifted = gskind_compile_string(pos, "*");
    kind = gskind_compile_string(pos, "**^*^");
    sv = gstypes_compile_type_var(pos, s, lifted);
    alphav = gstypes_compile_type_var(pos, alpha, lifted);
    abstract = gstypes_compile_abstract(pos, gsintern_string(gssymtypelable, "ibio.acceptor.prim.t"), kind);
    applied_abstract = gstype_apply(pos, gstype_apply(pos, abstract, sv), alphav);
    expected = gstypes_compile_lambda(pos, s, lifted, gstypes_compile_lambda(pos, alpha, lifted,
        gstypes_compile_lift(pos, gstypes_compile_sum(pos, 3,
            gsintern_string(gssymconstrlable, "fail"), gstypes_compile_ubproduct(pos, 0),
            gsintern_string(gssymconstrlable, "symbol.bind"), gstypes_compile_ubproduct(pos, 2,
                gsintern_string(gssymfieldlable, "0"), gstypes_compile_lift(pos, gstypes_compile_fun(pos, sv, applied_abstract)),
                gsintern_string(gssymfieldlable, "1"), applied_abstract
            ),
            gsintern_string(gssymconstrlable, "unit.plus"), gstypes_compile_ubproduct(pos, 2,
                gsintern_string(gssymfieldlable, "0"), alphav,
                gsintern_string(gssymfieldlable, "1"), applied_abstract
            )
        ))
    ));
    if (gstypes_type_check(err, pos, gstype_get_definition(pos, symtable, abstract), expected) < 0) {
        gsfinish_string_builder(err);
        gsfatal("%P: Panic!  ibio.acceptor.prim.t has the wrong structure: %s", pos, err->start);
    }
    gsfinish_string_builder(err);
}

/* §section Managing read threads */

static void ibio_read_threads_up(void);
static void ibio_read_threads_down(void);

#define IBIO_NUM_READ_THREADS 0x100

static struct ibio_read_thread_queue {
    Lock lock;
    int refcount;
    int numthreads;
    struct ibio_iport *iports[IBIO_NUM_READ_THREADS];
} *ibio_read_thread_queue;

static struct gs_sys_global_block_suballoc_info ibio_read_thread_queue_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Read Thread Queue",
    },
};

struct ibio_read_thread_args {
};

static void ibio_read_thread_main(void *);

static gs_sys_gc_post_callback ibio_read_thread_cleanup;
static gs_sys_gc_failure_callback ibio_read_thread_gc_failure_cleanup;

int
ibio_read_threads_init(char *err, char *eerr)
{
    struct ibio_read_thread_args ibio_read_thread_args;
    int readpid;

    if (ibio_read_thread_queue)
        gsfatal("ibio_read_threads_init called twice")
    ;

    ibio_read_thread_queue = gs_sys_global_block_suballoc(&ibio_read_thread_queue_info, sizeof(*ibio_read_thread_queue));

    memset(ibio_read_thread_queue, 0, sizeof(*ibio_read_thread_queue));

    ibio_read_threads_up();

    api_at_termination(ibio_read_threads_down);

    gs_sys_gc_post_callback_register(ibio_read_thread_cleanup);
    gs_sys_gc_failure_callback_register(ibio_read_thread_gc_failure_cleanup);

    ace_up();

    if ((readpid = gscreate_thread_pool(ibio_read_thread_main, &ibio_read_thread_args, sizeof(ibio_read_thread_args))) < 0) {
        seprint(err, eerr, "Couldn't create IBIO read thread process pool: %r");
        ace_down();
        ibio_read_threads_down();
        return -1;
    }

    return readpid;
}

static
void
ibio_read_thread_main(void *p)
{
    int have_clients, have_threads;

    do {
        gs_sys_gc_allow_collection(0);

        lock(&ibio_read_thread_queue->lock);
        have_clients = ibio_read_thread_queue->refcount > 0;
        have_threads = ibio_read_thread_queue->numthreads > 0;
        unlock(&ibio_read_thread_queue->lock);

        sleep(1);
    } while (have_clients || have_threads);

    ace_down();
}

static
int
ibio_read_thread_cleanup(struct gsstringbuilder *err)
{
    int i;
    struct ibio_read_thread_queue *new_read_thread_queue;

    new_read_thread_queue = gs_sys_global_block_suballoc(&ibio_read_thread_queue_info, sizeof(*ibio_read_thread_queue));
    memcpy(new_read_thread_queue, ibio_read_thread_queue, sizeof(*ibio_read_thread_queue));
    ibio_read_thread_queue = new_read_thread_queue;

    for (i = 0; i < IBIO_NUM_READ_THREADS; i++) {
        struct ibio_iport *iport;
        if (iport = ibio_read_thread_queue->iports[i]) {
            if (iport->forward) {
                ibio_read_thread_queue->iports[i] = iport = iport->forward;
            } else {
                gsvalue gcv, gctemp;

                gcv = (gsvalue)iport;
                if (GS_GC_TRACE(err, &gcv) < 0) return -1;
                ibio_read_thread_queue->iports[i] = iport = (struct ibio_iport *)gcv;

                iport->active = 0;
            }

            if (ibio_iptr_live(iport->position)) {
                iport->position = ibio_iptr_lookup_forward(iport->position);
                if (iport->channel->last_accessed_seg) iport->channel->last_accessed_seg = ibio_channel_segment_lookup_forward(iport->channel->last_accessed_seg);
            } else {
                if (ibio_iptr_trace(err, &iport->position) < 0) return -1;
                iport->channel->last_accessed_seg = 0;
            }
            if (iport->reading_thread) iport->reading_thread = api_thread_gc_forward(iport->reading_thread);
        }
    }

    return 0;
}

void
ibio_read_thread_gc_failure_cleanup()
{
    int i;
    struct ibio_iport *iport;

    for (i = 0; i < IBIO_NUM_READ_THREADS; i++) {
        if (iport = ibio_read_thread_queue->iports[i]) {
            if (iport->forward) ibio_read_thread_queue->iports[i] = iport = iport->forward;
            if (iport->reading_thread) iport->reading_thread = api_thread_gc_forward(iport->reading_thread);
        }
    }
}

static
void
ibio_read_threads_up()
{
    lock(&ibio_read_thread_queue->lock);
    ibio_read_thread_queue->refcount++;
    unlock(&ibio_read_thread_queue->lock);
}

static
void
ibio_read_threads_down()
{
    int i;

    lock(&ibio_read_thread_queue->lock);

    ibio_read_thread_queue->refcount--;
    if (!ibio_read_thread_queue->refcount) {
        for (i = 0; i < IBIO_NUM_READ_THREADS; i++) {
            if (ibio_read_thread_queue->iports[i]) {
                lock(&ibio_read_thread_queue->iports[i]->lock);
                ibio_read_thread_queue->iports[i]->active = 0;
                unlock(&ibio_read_thread_queue->iports[i]->lock);
            }
        }
    }

    unlock(&ibio_read_thread_queue->lock);
}

/* §section Opening files */

static struct ibio_iport *ibio_alloc_iport(void);

static void ibio_setup_iport_read_buffer(struct ibio_iport *);

struct ibio_read_process_args {
    struct ibio_iport *iport;
};

static void ibio_read_process_main(void *);

gsvalue
ibio_iport_fdopen(int fd, struct ibio_uxio *uxio, struct ibio_external_io *external, char *err, char *eerr)
{
    struct ibio_iport *res;
    struct ibio_read_process_args args;
    int readpid;

    res = ibio_alloc_iport();

    args.iport = res;

    res->fd = fd;
    res->uxio = uxio;
    res->external = external;
    ibio_setup_iport_read_buffer(res);

    unlock(&res->lock);

    ace_up();

    /* Create read process tied to res */
    if ((readpid = gscreate_thread_pool(ibio_read_process_main, &args, sizeof(args))) < 0) {
        seprint(err, eerr, "Couldn't create IBIO read process: %r");
        return 0;
    }

    return (gsvalue)res;
}

/* §section Reading from a file (read process) */

static void ibio_iport_unlink_from_thread(struct api_thread *, struct ibio_iport *);

static void ibio_shutdown_iport(struct ibio_iport *, gsvalue *);
static void ibio_shutdown_iport_on_read_symbol_unimpl(char *, int, struct gspos, struct ibio_iport *, struct ibio_channel_segment *, gsvalue *, char *, ...);

static
void
ibio_read_process_main(void *p)
{
    struct ibio_read_process_args *args;
    int i;
    int tid;
    int active, runnable;
    struct ibio_iport *iport;
    gsvalue *pos;

    args = (struct ibio_read_process_args *)p;

    iport = args->iport;

    tid = -1;
    lock(&ibio_read_thread_queue->lock);
    for (i = 0; i < IBIO_NUM_READ_THREADS; i++)
        if (!ibio_read_thread_queue->iports[i]) {
            ibio_read_thread_queue->iports[i] = iport;
            tid = i;
            ibio_read_thread_queue->numthreads++;
            break;
        }
    ;
    unlock(&ibio_read_thread_queue->lock);

    lock(&iport->lock);
    if (tid < 0) {
        gswarning("%s:%d: ibio_read_process_main: out of read threads", __FILE__, __LINE__);
        iport->active = 0;
        unlock(&iport->lock);
        ace_down();
        return;
    }
    unlock(&iport->lock);

    pos = 0;
    do {
        struct gsstringbuilder *err;
        int gcres;

        lock(&iport->lock);
        active = iport->active || iport->reading;
        runnable = !!iport->reading;

        err = gsreserve_string_builder();
        gcres = 0;
        if (gs_sys_gc_want_collection()) {
            if ((gcres = gs_sys_wait_for_collection_to_finish(err)) < 0) {
                gsfinish_string_builder(err);
                gswarning("GC failed: %s", err->start);
                err = gsreserve_string_builder();
            }

            if (iport->forward)
                iport = iport->forward
            ; else {
                gsstring_builder_print(err, "iport not traced in GC");
                gcres = -1;
            }
            if (pos) pos = ibio_iptr_lookup_forward(pos);

            gs_sys_gc_done_with_collection();
        }
        gsfinish_string_builder(err);
        if (active && (gcres < 0 || gs_sys_memory_exhausted())) {
            struct gsstringbuilder *msg;
            struct gspos gspos;

            msg = gsreserve_string_builder();
            if (gcres < 0) {
                gsstring_builder_print(msg, UNIMPL("GC failed: %s"), err->start);
            } else {
                gsstring_builder_print(msg, UNIMPL("Out of memory"));
            }
            gsfinish_string_builder(msg);

            gspos.file = gsintern_string(gssymfilename, __FILE__);
            gspos.lineno = __LINE__;

            if (iport->reading) api_thread_post_unimpl(iport->reading_thread, __FILE__, __LINE__, "%s", msg->start);
            iport->channel->error = (gsvalue)gsunimpl(__FILE__, __LINE__, gspos, "%s", msg->start);
            ibio_shutdown_iport(iport, iport->position);
            active = 0;
        }
        if (iport->reading) {
            gstypecode st;

            if (!pos) pos = iport->position;
            st = GS_SLOW_EVALUATE(iport->reading);
            switch (st) {
                case gstystack:
                    runnable = 0;
                    break;
                case gstywhnf: {
                    struct gsconstr_args *constr;

                    constr = (struct gsconstr_args *)iport->reading;
                    switch (constr->constrnum) {
                        case ibio_acceptor_fail:
                            iport->reading = 0;
                            pos = 0;
                            ibio_iport_unlink_from_thread(iport->reading_thread, iport);
                            break;
                        case ibio_acceptor_symbol_bind: {
                            struct ibio_channel_segment *seg;
                            gsvalue symk, eofk;

                            symk = constr->arguments[0];
                            eofk = constr->arguments[1];
                            seg = ibio_channel_segment_containing(pos);

                            if (pos < seg->extent) {
                                iport->reading = gsapply(constr->c.pos, constr->arguments[0], *pos);
                                if (pos + 1 < seg->extent) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->c.pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: can advance within segment");
                                    active = 0;
                                } else if (seg->next) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->c.pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: have next segment to advance to");
                                    active = 0;
                                } else if (seg->extent < ibio_channel_segment_limit(seg)) {
                                    pos++;
                                    unlock(&seg->lock);
                                } else {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->c.pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: cannot advance within segment");
                                    active = 0;
                                }
                            } else if (seg->next) {
                                pos = seg->next->items;
                                unlock(&seg->lock);

                                iport->reading = eofk;
                            } else if (iport->bufstart < iport->bufend) {
                                if (iport->external->canread(iport->external, iport->bufstart, iport->bufend)) {
                                    gsvalue sym;
                                    char err[0x100];

                                    iport->bufstart = iport->external->readsym(iport->external, err, err + sizeof(err), iport->bufstart, iport->bufend, &sym);
                                    if (!iport->bufstart) {
                                        api_thread_post(iport->reading_thread, "input decoding error: %s", err);
                                        ibio_shutdown_iport(iport, pos);
                                        active = 0;
                                        goto failed;
                                    }
                                    *pos++ = sym;
                                    seg->extent = pos;
                                    if (seg->extent >= ibio_channel_segment_limit(seg)) {
                                        struct ibio_channel_segment *newseg;

                                        lock(&iport->channel->lock);
                                        while (!(
                                            iport->waiting_to_read
                                            || api_thread_terminating(iport->reading_thread)
                                            || iport->channel->last_accessed_seg == seg
                                            || !iport->channel->last_accessed_seg
                                        )) {
                                            unlock(&iport->channel->lock);
                                            unlock(&seg->lock);
                                            unlock(&iport->lock);
                                            sleep(1);
                                            lock(&iport->lock);
                                            lock(&seg->lock);
                                            lock(&iport->channel->lock);
                                        }
                                        unlock(&iport->channel->lock);
                                        newseg = ibio_alloc_channel_segment(iport->channel);
                                        seg->next = newseg;
                                        unlock(&seg->lock);
                                        pos = newseg->items;
                                        unlock(&newseg->lock);
                                    }
                                    iport->reading = gsapply(constr->c.pos, symk, sym);
                                    unlock(&seg->lock);
                                } else {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->c.pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: not enough data in buffer to read a symbol");
                                    active = 0;
                                }
                            } else {
                                long n;

                                n = iport->uxio->refill(iport->uxio, iport->fd, iport->buf, (uchar*)iport->bufextent - (uchar*)iport->buf);
                                if (n < 0) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->c.pos, iport, seg, pos, "ibio_read_process_main: read failed: %r");
                                    active = 0;
                                    goto failed;
                                }
                                iport->bufstart = iport->buf;
                                iport->bufend = (uchar*)iport->buf + n;
                                if (n == 0) {
                                    struct ibio_channel_segment *newseg;

                                    newseg = ibio_alloc_channel_segment(iport->channel);
                                    seg->next = newseg;
                                    pos = newseg->items;
                                    unlock(&newseg->lock);
                                    unlock(&seg->lock);

                                    iport->reading = eofk;
                                } else {
                                    /* Loop and try again with the filled buffer */
                                    unlock(&seg->lock);
                                }
                            }
                        failed:
                            break;
                        }
                        case ibio_acceptor_unit_plus:
                            iport->position = pos;
                            iport->reading = constr->arguments[1];
                            break;
                        default:
                            api_thread_post_unimpl(iport->reading_thread, __FILE__, __LINE__, "ibio_read_process_main: constr = %d", constr->constrnum);
                            ibio_shutdown_iport(iport, pos);
                            active = 0;
                            break;
                    }
                    break;
                }
                case gstyindir:
                    iport->reading = GS_REMOVE_INDIRECTION(iport->reading);
                    break;
                case gstyerr: {
                    struct gserror *err;
                    char buf[0x100];

                    err = (struct gserror *)iport->reading;
                    gserror_format(buf, buf + sizeof(buf), err);
                    api_thread_post(iport->reading_thread, "acceptor error: %s", buf);
                    lock(&iport->channel->lock);
                    iport->channel->error = (gsvalue)gserror(err->pos, "acceptor error: %s", buf);
                    unlock(&iport->channel->lock);

                    ibio_shutdown_iport(iport, pos);
                    active = 0;
                    break;
                }
                case gstyimplerr: {
                    struct gsimplementation_failure *err;
                    char buf[0x100];

                    err = (struct gsimplementation_failure *)iport->reading;
                    gsimplementation_failure_format(buf, buf + sizeof(buf), err);
                    api_thread_post(iport->reading_thread, "unimplemented acceptor: %s", buf);
                    lock(&iport->channel->lock);
                    iport->channel->error = (gsvalue)err;
                    unlock(&iport->channel->lock);

                    ibio_shutdown_iport(iport, pos);
                    active = 0;
                    break;
                }
                default:
                    api_thread_post_unimpl(iport->reading_thread, __FILE__, __LINE__, "ibio_read_process_main: st = %d", st);
                    ibio_shutdown_iport(iport, pos);
                    active = 0;
                    break;
            }
        }
        unlock(&iport->lock);
        if (active && !runnable)
            sleep(1)
        ;
    } while (active);

    if (iport->fd > 2)
        close(iport->fd)
    ;
    lock(&ibio_read_thread_queue->lock);
    for (i = 0; i < IBIO_NUM_READ_THREADS; i++)
        if (ibio_read_thread_queue->iports[i] == iport)
            ibio_read_thread_queue->iports[i] = 0
    ;
    ibio_read_thread_queue->numthreads--;
    unlock(&ibio_read_thread_queue->lock);

    ace_down();
}

static
void
ibio_shutdown_iport_on_read_symbol_unimpl(char *file, int lineno, struct gspos gspos, struct ibio_iport *iport, struct ibio_channel_segment *seg, gsvalue *pos, char *fmt, ...)
{
    struct gsstringbuilder *msg;
    va_list arg;

    unlock(&seg->lock);

    msg = gsreserve_string_builder();
    va_start(arg, fmt);
    gsstring_builder_vprint(msg, fmt, arg);
    va_end(arg);
    gsfinish_string_builder(msg);

    api_thread_post_unimpl(iport->reading_thread, file, lineno, "%s", msg->start);
    lock(&iport->channel->lock);
    iport->channel->error = (gsvalue)gsunimpl(file, lineno, gspos, "%s", msg->start);
    unlock(&iport->channel->lock);
    ibio_shutdown_iport(iport, pos);
}

static
void
ibio_shutdown_iport(struct ibio_iport *iport, gsvalue *pos)
{
    iport->active = 0;
    if (iport->reading) ibio_iport_unlink_from_thread(iport->reading_thread, iport);
    iport->reading = 0;
}

/* §section Reading (from API thread) */

static void ibio_iport_link_to_thread(struct api_thread *, struct ibio_iport *);

struct ibio_read_blocking {
    struct api_prim_blocking bl;
    struct ibio_iport *iport;
    gsvalue acceptor;
    struct ibio_iport_read_blocker *blocking;
};

static struct gs_sys_global_block_suballoc_info ibio_read_blocking_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Read Blocking Queue Link",
    },
};

struct ibio_iport_read_blocker {
    struct ibio_iport_read_blocker *next;
};

static api_prim_blocking_gccopy ibio_read_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_read_blocking_gcevacuate;
static api_prim_blocking_gccleanup ibio_read_blocking_gccleanup;

enum api_prim_execution_state
ibio_handle_prim_read(struct api_thread *thread, struct gseprim *read, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    gsvalue res;
    struct ibio_read_blocking *read_blocking;

    if (*pblocking) {
        read_blocking = (struct ibio_read_blocking *)*pblocking;
    } else {
        *pblocking = api_blocking_alloc(sizeof(struct ibio_read_blocking), ibio_read_blocking_gccopy, ibio_read_blocking_gcevacuate, ibio_read_blocking_gccleanup);
        read_blocking = (struct ibio_read_blocking *)*pblocking;
        read_blocking->iport = (struct ibio_iport*)read->p.arguments[0];
        read_blocking->acceptor = read->p.arguments[1];
        read_blocking->blocking = 0;
    }

    res = 0;
    lock(&read_blocking->iport->lock);

    if (!read_blocking->iport->active) {
        api_abend(thread, "read on inactive iport: %p", read_blocking->iport);
        unlock(&read_blocking->iport->lock);
        return api_st_error;
    }
    if (!read_blocking->iport->reading) {
        struct ibio_channel_segment *seg;

        if (read_blocking->blocking) {
            if (read_blocking->blocking == read_blocking->iport->waiting_to_read) {
                read_blocking->iport->waiting_to_read = read_blocking->iport->waiting_to_read->next;
                if (!read_blocking->iport->waiting_to_read)
                    read_blocking->iport->waiting_to_read_end = &read_blocking->iport->waiting_to_read
                ;
            } else {
                unlock(&read_blocking->iport->lock);
                return api_st_blocked;
            }
        }
        read_blocking->iport->reading = read_blocking->acceptor;
        read_blocking->iport->reading_thread = thread;
        ibio_iport_link_to_thread(thread, read_blocking->iport);

        res = (gsvalue)read_blocking->iport->position;
        seg = ibio_channel_segment_containing(read_blocking->iport->position);
        lock(&read_blocking->iport->channel->lock);
        read_blocking->iport->channel->last_accessed_seg = seg;
        unlock(&read_blocking->iport->channel->lock);
        unlock(&seg->lock);

        unlock(&read_blocking->iport->lock);
        *pv = res;
        return api_st_success;
    } else if (!read_blocking->blocking) {
        read_blocking->blocking =
            *read_blocking->iport->waiting_to_read_end =
                gs_sys_global_block_suballoc(&ibio_read_blocking_info, sizeof(struct ibio_iport_read_blocker))
        ;

        read_blocking->iport->waiting_to_read_end = &read_blocking->blocking->next;
        read_blocking->blocking->next = 0;

        unlock(&read_blocking->iport->lock);
        return api_st_blocked;
    } else {
        unlock(&read_blocking->iport->lock);
        return api_st_blocked;
    }
}

static
struct api_prim_blocking *
ibio_read_blocking_gccopy(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_read_blocking_gccopy"));
    return 0;
}

int
ibio_read_blocking_gcevacuate(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_read_blocking_gc_trace"));
    return -1;
}

void
ibio_read_blocking_gccleanup(struct api_prim_blocking *blocking)
{
}

/* §section Reading (Global Script-side) */

int
ibio_prim_iptr_handle_iseof(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    struct ibio_channel_segment *seg;
    gsvalue iptrv, *iptr;

    iptrv = args[0];
    iptr = (gsvalue *)iptrv;

    seg = ibio_channel_segment_containing(iptr);

    if (iptr < seg->extent) {
        unlock(&seg->lock);
        return gsprim_return_ubsum(thread, pos, 0, 0);
    } else {
        unlock(&seg->lock);
        return gsprim_return_ubsum(thread, pos, 1, 0);
    }
}

/* §subsection §gs{ibio.prim.iptr.deref} */

struct ibio_prim_iptr_deref_blocking {
    struct gslprim_blocking gs;
    gsvalue res;
};

static int ibio_prim_iptr_deref_return(struct ace_thread *, struct gspos, gsvalue, struct ibio_prim_iptr_deref_blocking *);
static gslprim_gccopy_handler ibio_prim_iptr_gccopy_deref;

int
ibio_prim_iptr_handle_deref(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    struct ibio_channel_segment *seg;
    gsvalue iptrv, *iptr;

    iptrv = args[0];
    iptr = (gsvalue *)iptrv;

    seg = ibio_channel_segment_containing(iptr);

    if (iptr < seg->extent) {
        gsvalue v;

        v = *iptr;
        unlock(&seg->lock);

        return ibio_prim_iptr_deref_return(thread, pos, v, 0);
    } else if (seg->next) {
        unlock(&seg->lock);
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "ibio_prim_iptr_handle_deref: points at EOF");
    } else {
        /* Can't happen!  §ccode{iptr} is necessarily a §gs{iptr α} so fulfilled */
        unlock(&seg->lock);
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "ibio_prim_iptr_handle_deref: still un-fulfilled");
    }
}

static
int
ibio_prim_iptr_resume_deref(struct ace_thread *thread, struct gspos pos, struct gslprim_blocking *gsblocking)
{
    return ibio_prim_iptr_deref_return(thread, pos, 0, (struct ibio_prim_iptr_deref_blocking *)gsblocking);
}

static
int
ibio_prim_iptr_deref_return(struct ace_thread *thread, struct gspos pos, gsvalue v, struct ibio_prim_iptr_deref_blocking *blocking)
{
    gstypecode st;

    if (!v) v = blocking->res;

    for (;;) {
        st = GS_SLOW_EVALUATE(v);
        switch (st) {
            case gstystack: {
                if (!blocking) {
                    blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_deref, ibio_prim_iptr_gccopy_deref);
                    blocking->res = v;
                }

                return gsprim_block(thread, pos, (struct gslprim_blocking *)blocking);
            }
            case gstywhnf:
                return gsprim_return(thread, pos, v);
            case gstyindir:
                blocking->res = v = GS_REMOVE_INDIRECTION(v);
                break;
            case gstyunboxed:
                return gsprim_return(thread, pos, v);
            default:
                return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "ibio_prim_iptr_deref_return: st = %d", st);
        }
    }
}

struct gslprim_blocking *
ibio_prim_iptr_gccopy_deref(struct gsstringbuilder *err, struct gslprim_blocking *gsblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_prim_iptr_gccopy_deref"));
    return 0;
}

/* §subsection §gs{ibio.prim.iptr.next} */

static gslprim_resumption_handler ibio_prim_iptr_resume_next, ibio_prim_iptr_resume_next_wait_for_seg;

struct ibio_prim_iptr_next_blocking {
    struct gslprim_blocking gs;
    gsvalue *iptr_res;
};

struct ibio_prim_iptr_next_blocking_wait_for_seg {
    struct gslprim_blocking gs;
    struct ibio_channel_segment *seg;
};

static int ibio_prim_iptr_next_return(struct ace_thread *, struct gspos, struct ibio_channel_segment *, gsvalue *, struct ibio_prim_iptr_next_blocking *);
static int ibio_prim_iptr_next_return_wait_for_seg(struct ace_thread *, struct gspos, struct ibio_channel_segment *, struct ibio_prim_iptr_next_blocking_wait_for_seg *);
static gslprim_gccopy_handler ibio_prim_iptr_next_gccopy, ibio_prim_iptr_next_wait_for_seg_gccopy;

int
ibio_prim_iptr_handle_next(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    struct ibio_channel_segment *seg;
    gsvalue iptrv, *iptr;

    iptrv = args[0];
    iptr = (gsvalue *)iptrv;

    seg = ibio_channel_segment_containing(iptr);

    if (iptr == seg->extent || iptr + 1 == ibio_channel_segment_limit(seg)) {
        return ibio_prim_iptr_next_return_wait_for_seg(thread, pos, seg, 0);
    } else {
        return ibio_prim_iptr_next_return(thread, pos, seg, iptr + 1, 0);
    }
}

int
ibio_prim_iptr_resume_next(struct ace_thread *thread, struct gspos pos, struct gslprim_blocking *gsblocking)
{
    return ibio_prim_iptr_next_return(thread, pos, 0, 0, (struct ibio_prim_iptr_next_blocking *)gsblocking);
}

static
int
ibio_prim_iptr_next_return(struct ace_thread *thread, struct gspos pos, struct ibio_channel_segment *seg, gsvalue *iptr_res, struct ibio_prim_iptr_next_blocking *blocking)
{
    if (!iptr_res) {
        iptr_res = blocking->iptr_res;

        seg = ibio_channel_segment_containing(iptr_res);
    }

    if (iptr_res < seg->extent || seg->next) {
        unlock(&seg->lock);
        return gsprim_return(thread, pos, (gsvalue)iptr_res);
    } else if (seg->channel->error) {
        gsvalue res;

        res = seg->channel->error;
        unlock(&seg->lock);
        return gsprim_error(thread, (struct gserror *)res);
    } else {
        unlock(&seg->lock);

        if (!blocking) {
            blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_next, ibio_prim_iptr_next_gccopy);
            blocking->iptr_res = iptr_res;
        }

        return gsprim_block(thread, pos, (struct gslprim_blocking *)blocking);
    }
}

struct gslprim_blocking *
ibio_prim_iptr_next_gccopy(struct gsstringbuilder *err, struct gslprim_blocking *gsblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_prim_iptr_next_gccopy"));
    return 0;
}

int
ibio_prim_iptr_resume_next_wait_for_seg(struct ace_thread *thread, struct gspos pos, struct gslprim_blocking *gsblocking)
{
    return ibio_prim_iptr_next_return_wait_for_seg(thread, pos, 0, (struct ibio_prim_iptr_next_blocking_wait_for_seg *)gsblocking);
}

static
int
ibio_prim_iptr_next_return_wait_for_seg(struct ace_thread *thread, struct gspos pos, struct ibio_channel_segment *seg, struct ibio_prim_iptr_next_blocking_wait_for_seg *blocking)
{
    if (!seg) {
        seg = blocking->seg;
        lock(&seg->lock);
    }

    if (seg->next) {
        struct ibio_channel_segment *nextseg;
        gsvalue *iptr_res;
        struct ibio_channel *channel;

        nextseg = seg->next;
        unlock(&seg->lock);
        lock(&nextseg->lock);
        iptr_res = nextseg->items;
        channel = seg->channel;

        lock(&channel->lock);
        if (channel->last_accessed_seg == seg) channel->last_accessed_seg = nextseg;
        unlock(&channel->lock);

        return ibio_prim_iptr_next_return(thread, pos, nextseg, iptr_res, 0);
    } else {
        unlock(&seg->lock);
        if (!blocking) {
            blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_next_wait_for_seg, ibio_prim_iptr_next_wait_for_seg_gccopy);
            blocking->seg = seg;
        }
        return gsprim_block(thread, pos, (struct gslprim_blocking *)blocking);
    }
}

struct gslprim_blocking *
ibio_prim_iptr_next_wait_for_seg_gccopy(struct gsstringbuilder *err, struct gslprim_blocking *gsblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_prim_iptr_next_wait_for_seg_gccopy"));
    return 0;
}

/* §section Associating list of current reads to thread */

struct ibio_thread_to_iport_link {
    struct ibio_thread_to_iport_link *next;
    struct ibio_iport *iport;
};

static struct gs_sys_global_block_suballoc_info ibio_thread_to_iport_link_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Thread-to-Iport link",
    },
};

static
void
ibio_iport_link_to_thread(struct api_thread *thread, struct ibio_iport *iport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_iport_link *link;

    data = api_thread_client_data(thread);

    link = gs_sys_global_block_suballoc(&ibio_thread_to_iport_link_info, sizeof(*link));

    link->next = data->reading_from_iport;
    link->iport = iport;
    data->reading_from_iport = link;
}

static
void
ibio_iport_unlink_from_thread(struct api_thread *thread, struct ibio_iport *iport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_iport_link **p;

    api_take_thread(thread);

    data = api_thread_client_data(thread);
    for (p = &data->reading_from_iport; *p; p = &(*p)->next) {
        if ((*p)->iport == iport) {
            *p = (*p)->next;
            api_release_thread(thread);
            return;
        }
    }

    gswarning("%s:%d: Thread %p not actually associated to iport %p", __FILE__, __LINE__, thread, iport);
    api_release_thread(thread);
}

int
ibio_thread_to_iport_link_trace(struct gsstringbuilder *err, struct ibio_thread_to_iport_link **plink)
{
    struct ibio_thread_to_iport_link *newlink;
    gsvalue gcv, gctemp;

    newlink = gs_sys_global_block_suballoc(&ibio_thread_to_iport_link_info, sizeof(*newlink));

    memcpy(newlink, *plink, sizeof(*newlink));

    *plink = newlink;
    if (newlink->next) {
        gsstring_builder_print(err, UNIMPL("ibio_thread_to_iport_link_trace: evacuate next"));
        return -1;
    }

    gcv = (gsvalue)newlink->iport;
    if (GS_GC_TRACE(err, &gcv) < 0) return -1;
    newlink->iport = (struct ibio_iport *)gcv;

    return 0;
}

void
ibio_thread_to_iport_link_cleanup(struct ibio_thread_to_iport_link **plink)
{
    struct ibio_thread_to_iport_link *link;

    for (link = *plink; link; link = link->next)
        if (link->iport->forward) link->iport = link->iport->forward
    ;
}

/* §section Allocation */

static gsvalue ibio_iport_trace(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info ibio_iport_segment_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ ibio_iport_trace,
        /* description = */ "IBIO iports",
    },
};

static
struct ibio_iport *
ibio_alloc_iport()
{
    struct ibio_iport *res;
    struct ibio_channel_segment *seg;

    res = gs_sys_global_block_suballoc(&ibio_iport_segment_info, sizeof(*res));

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;
    res->channel = ibio_alloc_channel();

    res->reading = 0;
    res->reading_thread = 0;
    res->waiting_to_read = 0;
    res->waiting_to_read_end = &res->waiting_to_read;

    seg = ibio_alloc_channel_segment(res->channel);

    res->position = seg->items;
    lock(&res->channel->lock);
    res->channel->last_accessed_seg = seg;
    unlock(&res->channel->lock);
    unlock(&seg->lock);

    res->forward = 0;

    return res;
}

static int ibio_iport_read_buffer_evacuate(struct gsstringbuilder *, struct ibio_iport *);

static
gsvalue
ibio_iport_trace(struct gsstringbuilder *err, gsvalue v)
{
    struct ibio_iport *iport, *newiport;
    gsvalue gctemp;

    iport = (struct ibio_iport *)v;

    if (iport->forward) return (gsvalue)iport->forward;

    newiport = gs_sys_global_block_suballoc(&ibio_iport_segment_info, sizeof(*newiport));

    memcpy(newiport, iport, sizeof(*iport));
    memset(&newiport->lock, 0, sizeof(newiport->lock));
    newiport->forward = 0;

    iport->forward = newiport;

    if (GS_GC_TRACE(err, &newiport->reading) < 0) return 0;

    if (newiport->waiting_to_read) {
        gsstring_builder_print(err, UNIMPL("ibio_iport_trace: evacuate result: waiting_to_read"));
        return 0;

        gsstring_builder_print(err, UNIMPL("ibio_iport_trace: evacuate result: waiting_to_read_end"));
        return 0;
    } else {
        newiport->waiting_to_read_end = &newiport->waiting_to_read;
    }

    if (ibio_channel_trace(err, &newiport->channel) < 0) return 0;
    if (ibio_uxio_trace(err, &newiport->uxio) < 0) return 0;
    if (ibio_external_io_trace(err, &newiport->external) < 0) return 0;

    if (ibio_iport_read_buffer_evacuate(err, newiport) < 0) return 0;

    return (gsvalue)newiport;
}

#define IBIO_READ_BUFFER_SIZE 0x10000

static struct gs_sys_aligned_block_suballoc_info ibio_iport_read_buffer_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Read Buffers",
    },
    /* align = */ IBIO_READ_BUFFER_SIZE,
};

static
void
ibio_setup_iport_read_buffer(struct ibio_iport *iport)
{
    gs_sys_aligned_block_suballoc(&ibio_iport_read_buffer_info, &iport->buf, &iport->bufextent);
    iport->bufstart = iport->bufend = iport->buf;
}

static
int
ibio_iport_read_buffer_evacuate(struct gsstringbuilder *err, struct ibio_iport *iport)
{
    void *oldbuf, *oldbufstart, *oldbufend, *oldbufextent;

    oldbuf = iport->buf;
    oldbufstart = iport->bufstart;
    oldbufend = iport->bufend;
    oldbufextent = iport->bufextent;

    ibio_setup_iport_read_buffer(iport);

    if ((uchar*)oldbufend - (uchar*)oldbufstart > (uchar*)iport->bufextent - (uchar*)iport->buf) {
        gsstring_builder_print(err, UNIMPL("ibio_iport_trace: evacuate result: new buf too small"));
        return -1;
    }

    memcpy(iport->buf, oldbufstart, (uchar*)oldbufend - (uchar*)oldbufstart);

    iport->bufend = (uchar*)iport->buf + ((uchar*)oldbufend - (uchar*)oldbufstart);

    return 0;
}
