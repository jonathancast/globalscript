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
    struct gsstringbuilder err;

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
    if (gstypes_type_check(&err, pos, gstype_get_definition(pos, symtable, abstract), expected) < 0) {
        gsfinish_string_builder(&err);
        gsfatal("%P: Panic!  ibio.acceptor.prim.t has the wrong structure: %s", pos, err.start);
    }
    gsfinish_string_builder(&err);
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

static struct gs_block_class ibio_read_thread_queue_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "IBIO Read Thread Queue",
};
static void *ibio_read_thread_queue_nursury;

struct ibio_read_thread_args {
};

static void ibio_read_thread_main(void *);

int
ibio_read_threads_init(char *err, char *eerr)
{
    struct ibio_read_thread_args ibio_read_thread_args;
    int readpid;

    if (ibio_read_thread_queue)
        gsfatal("ibio_read_threads_init called twice")
    ;

    ibio_read_thread_queue = gs_sys_block_suballoc(&ibio_read_thread_queue_descr, &ibio_read_thread_queue_nursury, sizeof(*ibio_read_thread_queue), sizeof(void*));

    memset(ibio_read_thread_queue, 0, sizeof(*ibio_read_thread_queue));

    ibio_read_threads_up();

    api_at_termination(ibio_read_threads_down);

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
        lock(&ibio_read_thread_queue->lock);
        have_clients = ibio_read_thread_queue->refcount > 0;
        have_threads = ibio_read_thread_queue->numthreads > 0;
        unlock(&ibio_read_thread_queue->lock);
    } while (have_clients || have_threads);

    ace_down();
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

/* ↓ Caller must have channel argument locked */
static struct ibio_iport *ibio_alloc_iport(struct ibio_channel_segment *);

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
    struct ibio_channel_segment *chan;

    chan = ibio_alloc_channel_segment();

    res = ibio_alloc_iport(chan);

    args.iport = res;

    res->fd = fd;
    res->uxio = uxio;
    res->external = external;
    ibio_setup_iport_read_buffer(res);

    unlock(&chan->lock);
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
        lock(&iport->lock);
        active = iport->active || iport->reading;
        runnable = !!iport->reading;

        if (active && gs_sys_memory_exhausted()) {
            struct gsstringbuilder msg;
            struct gspos gspos;

            msg = gsreserve_string_builder();
            gsstring_builder_print(&msg, UNIMPL("Out of memory"));
            gsfinish_string_builder(&msg);

            gspos.file = gsintern_string(gssymfilename, __FILE__);
            gspos.lineno = __LINE__;

            if (iport->reading) api_thread_post_unimpl(iport->reading_thread, __FILE__, __LINE__, "%s", msg.start);
            iport->error = (gsvalue)gsunimpl(__FILE__, __LINE__, gspos, "%s", msg.start);
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
                    struct gsconstr *constr;

                    constr = (struct gsconstr *)iport->reading;
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
                                iport->reading = gsapply(constr->pos, constr->arguments[0], *pos);
                                if (pos + 1 < seg->extent) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: can advance within segment");
                                    active = 0;
                                } else if (seg->next) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: have next segment to advance to");
                                    active = 0;
                                } else if (seg->extent < ibio_channel_segment_limit(seg)) {
                                    pos++;
                                    unlock(&seg->lock);
                                } else {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: cannot advance within segment");
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

                                        while (!(
                                            iport->waiting_to_read
                                            || 1 /* §todo{Need to check if thread is waiting on output instead} */
                                            || iport->last_accessed_seg == seg
                                        )) {
                                            unlock(&seg->lock);
                                            unlock(&iport->lock);
                                            sleep(1);
                                            lock(&iport->lock);
                                            lock(&seg->lock);
                                        }
                                        newseg = ibio_alloc_channel_segment();
                                        newseg->iport = iport;
                                        seg->next = newseg;
                                        unlock(&seg->lock);
                                        pos = newseg->items;
                                        unlock(&newseg->lock);
                                    }
                                    iport->reading = gsapply(constr->pos, symk, sym);
                                    unlock(&seg->lock);
                                } else {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: not enough data in buffer to read a symbol");
                                    active = 0;
                                }
                            } else {
                                long n;

                                n = iport->uxio->refill(iport->uxio, iport->fd, iport->buf, (uchar*)iport->bufextent - (uchar*)iport->buf);
                                if (n < 0) {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: read failed: %r");
                                    active = 0;
                                    goto failed;
                                }
                                iport->bufstart = iport->buf;
                                iport->bufend = (uchar*)iport->buf + n;
                                if (n == 0) {
                                    struct ibio_channel_segment *newseg;

                                    newseg = ibio_alloc_channel_segment();
                                    newseg->iport = iport;
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
                    iport->error = (gsvalue)gserror(err->pos, "acceptor error: %s", buf);

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
                    iport->error = (gsvalue)err;

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
    struct gsstringbuilder msg;
    va_list arg;

    unlock(&seg->lock);

    msg = gsreserve_string_builder();
    va_start(arg, fmt);
    gsstring_builder_vprint(&msg, fmt, arg);
    va_end(arg);
    gsfinish_string_builder(&msg);

    api_thread_post_unimpl(iport->reading_thread, file, lineno, "%s", msg.start);
    iport->error = (gsvalue)gsunimpl(file, lineno, gspos, "%s", msg.start);
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

static struct gs_block_class ibio_read_blocking_class = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "IBIO Read Blocking Queue Link",
};
static void *ibio_read_blocking_nursury;
static Lock ibio_read_blocking_lock;

struct ibio_iport_read_blocker {
    struct ibio_iport_read_blocker *next;
};

enum api_prim_execution_state
ibio_handle_prim_read(struct api_thread *thread, struct gseprim *read, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    gsvalue res;
    struct ibio_read_blocking *read_blocking;

    if (*pblocking) {
        read_blocking = (struct ibio_read_blocking *)*pblocking;
    } else {
        *pblocking = api_blocking_alloc(sizeof(struct ibio_read_blocking));
        read_blocking = (struct ibio_read_blocking *)*pblocking;
        read_blocking->iport = (struct ibio_iport*)read->arguments[0];
        read_blocking->acceptor = read->arguments[1];
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

        unlock(&read_blocking->iport->lock);
        *pv = res;
        return api_st_success;
    } else if (!read_blocking->blocking) {
        lock(&ibio_read_blocking_lock);
        read_blocking->blocking = *read_blocking->iport->waiting_to_read_end = gs_sys_block_suballoc(&ibio_read_blocking_class, &ibio_read_blocking_nursury, sizeof(struct ibio_iport_read_blocker), sizeof(void *));
        unlock(&ibio_read_blocking_lock);

        read_blocking->iport->waiting_to_read_end = &read_blocking->blocking->next;
        read_blocking->blocking->next = 0;

        unlock(&read_blocking->iport->lock);
        return api_st_blocked;
    } else {
        unlock(&read_blocking->iport->lock);
        return api_st_blocked;
    }
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

/* §section §gs{ibio.prim.iptr.deref} */

struct ibio_prim_iptr_deref_blocking {
    struct gslprim_blocking gs;
    gsvalue res;
};

static int ibio_prim_iptr_deref_return(struct ace_thread *, struct gspos, gsvalue, struct ibio_prim_iptr_deref_blocking *);

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
                    blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_deref);
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

/* §section §gs{ibio.prim.iptr.next} */

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
    } else if (seg->iport->error) {
        gsvalue res;

        res = seg->iport->error;
        unlock(&seg->lock);
        return gsprim_error(thread, (struct gserror *)res);
    } else if (seg->iport->error) {
        gsvalue res;

        res = seg->iport->error;
        unlock(&seg->lock);
        return gsprim_error(thread, (struct gserror *)res);
    } else {
        unlock(&seg->lock);

        if (!blocking) {
            blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_next);
            blocking->iptr_res = iptr_res;
        }

        return gsprim_block(thread, pos, (struct gslprim_blocking *)blocking);
    }
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
        struct ibio_iport *iport;

        nextseg = seg->next;
        unlock(&seg->lock);
        lock(&nextseg->lock);
        iptr_res = nextseg->items;
        iport = seg->iport;

        lock(&iport->lock);
        if (iport->last_accessed_seg == seg) iport->last_accessed_seg = nextseg;
        unlock(&iport->lock);

        return ibio_prim_iptr_next_return(thread, pos, nextseg, iptr_res, 0);
    } else {
        unlock(&seg->lock);
        if (!blocking) {
            blocking = gslprim_blocking_alloc(sizeof(*blocking), ibio_prim_iptr_resume_next_wait_for_seg);
            blocking->seg = seg;
        }
        return gsprim_block(thread, pos, (struct gslprim_blocking *)blocking);
    }
}

/* §section Associating list of current reads to thread */

struct ibio_thread_to_iport_link {
    struct ibio_thread_to_iport_link *next;
    struct ibio_iport *iport;
};

static struct gs_block_class ibio_thread_to_iport_link_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "IBIO Oports",
};
static void *ibio_thread_to_iport_link_nursury;
static Lock ibio_thread_to_iport_link_lock;

static
void
ibio_iport_link_to_thread(struct api_thread *thread, struct ibio_iport *iport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_iport_link *link;

    data = api_thread_client_data(thread);

    lock(&ibio_thread_to_iport_link_lock);
    link = gs_sys_block_suballoc(&ibio_thread_to_iport_link_descr, &ibio_thread_to_iport_link_nursury, sizeof(*link), sizeof(void*));
    unlock(&ibio_thread_to_iport_link_lock);

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

/* §section Allocation */

static struct gs_sys_global_block_suballoc_info ibio_iport_segment_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO iports",
    },
};

static
struct ibio_iport *
ibio_alloc_iport(struct ibio_channel_segment *chan)
{
    struct ibio_iport *res;

    res = gs_sys_global_block_suballoc(&ibio_iport_segment_info, sizeof(*res));

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;
    res->error = 0;

    res->reading = 0;
    res->reading_thread = 0;
    res->waiting_to_read = 0;
    res->waiting_to_read_end = &res->waiting_to_read;

    res->position = chan->items;
    chan->iport = res;
    res->last_accessed_seg = chan;

    return res;
}

#define IBIO_READ_BUFFER_SIZE 0x10000

static struct gs_block_class ibio_iport_read_buffer_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "IBIO Read Buffers",
};
static void *ibio_iport_read_buffer_nursury;
static Lock ibio_iport_read_buffer_lock;
static int ibio_iport_read_buffer_pre_gc_callback_registered;
static gs_sys_gc_pre_callback ibio_iport_read_buffer_pre_gc_callback;

static
void
ibio_setup_iport_read_buffer(struct ibio_iport *iport)
{
    struct gs_blockdesc *nursury_block;
    void *bufbase, *buf, *bufextent;

    lock(&ibio_iport_read_buffer_lock);
    {
        if (ibio_iport_read_buffer_nursury) {
            buf = ibio_iport_read_buffer_nursury;
            nursury_block = BLOCK_CONTAINING(buf);
        } else {
            if (!ibio_iport_read_buffer_pre_gc_callback_registered) {
                gs_sys_gc_pre_callback_register(ibio_iport_read_buffer_pre_gc_callback);
                ibio_iport_read_buffer_pre_gc_callback_registered = 1;
            }
            nursury_block = gs_sys_block_alloc(&ibio_iport_read_buffer_descr);
            buf = START_OF_BLOCK(nursury_block);
        }
        bufbase = (uchar*)buf;
        if ((uintptr)bufbase % IBIO_READ_BUFFER_SIZE)
            bufbase = (uchar*)bufbase - ((uintptr)bufbase % IBIO_READ_BUFFER_SIZE)
        ;
        bufextent = (uchar*)bufbase + IBIO_READ_BUFFER_SIZE;
        if ((uchar*)bufextent >= (uchar*)END_OF_BLOCK(nursury_block))
            ibio_iport_read_buffer_nursury = 0
        ; else
            ibio_iport_read_buffer_nursury = bufextent
        ;
    }
    unlock(&ibio_iport_read_buffer_lock);

    iport->buf = buf;
    iport->bufstart = buf;
    iport->bufend = buf;
    iport->bufextent = bufextent;
}

static
void
ibio_iport_read_buffer_pre_gc_callback()
{
    ibio_iport_read_buffer_nursury = 0;
}
