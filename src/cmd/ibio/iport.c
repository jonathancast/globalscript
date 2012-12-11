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
ibio_check_acceptor_type(char *script, struct gsfile_symtable *symtable, struct gspos pos)
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
        ace_down();
        gsfatal("%s: Panic!  ibio.acceptor.prim.t has the wrong structure: %s", script, err.start);
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

    ibio_read_thread_queue = gs_sys_seg_suballoc(&ibio_read_thread_queue_descr, &ibio_read_thread_queue_nursury, sizeof(*ibio_read_thread_queue), sizeof(void*));

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
ibio_iport_fdopen(int fd, struct ibio_external_io *external, char *err, char *eerr)
{
    struct ibio_iport *res;
    struct ibio_read_process_args args;
    int readpid;
    struct ibio_channel_segment *chan;

    chan = ibio_alloc_channel_segment();

    res = ibio_alloc_iport(chan);

    args.iport = res;

    res->fd = fd;
    res->refill = read;
    res->external = external;
    ibio_setup_iport_read_buffer(res);

    unlock(&chan->lock);
    unlock(&res->lock);

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
        return;
    }
    unlock(&iport->lock);

    pos = 0;
    do {
        lock(&iport->lock);
        active = iport->active || iport->reading;
        runnable = !!iport->reading;

        if (iport->reading) {
            gstypecode st;

            if (!pos) pos = iport->position;
            st = GS_SLOW_EVALUATE(iport->reading);
            switch (st) {
                case gstystack:
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

                            seg = ibio_channel_segment_containing(pos);

                            if (pos < seg->extent) {
                                ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: have symbol");
                                active = 0;
                            } else if (seg->next) {
                                ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: eof");
                                active = 0;
                            } else if (iport->bufstart < iport->bufend) {
                                if (iport->external->canread(iport->external, iport->bufstart, iport->bufend)) {
                                    gsvalue sym;
                                    char err[0x100];

                                    iport->bufstart = iport->external->readsym(iport->external, err, err + sizeof(err), iport->bufstart, iport->bufend, pos);
                                    if (!iport->bufstart) {
                                        ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: decoding error: %s");
                                        active = 0;
                                    }
                                    sym = *pos;
                                    pos = seg->extent++;
                                    if (seg->extent >= ibio_channel_segment_limit(seg)) {
                                        ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: allocate a new buffer");
                                        active = 0;
                                    }
                                    iport->reading = gsapply(constr->pos, constr->arguments[0], sym);
                                    unlock(&seg->lock);
                                } else {
                                    ibio_shutdown_iport_on_read_symbol_unimpl(__FILE__, __LINE__, constr->pos, iport, seg, pos, "ibio_read_process_main: symbol.bind: not enough data in buffer to read a symbol");
                                    active = 0;
                                }
                            } else {
                                long n;

                                n = iport->refill(iport->fd, iport->buf, (uchar*)iport->bufextent - (uchar*)iport->buf);
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

                                    iport->reading = constr->arguments[1];
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
                    iport->reading = GS_REMOVE_INDIRECTIONS(iport->reading);
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
    iport->reading = 0;
    ibio_iport_unlink_from_thread(iport->reading_thread, iport);
}

/* §section Reading (from API thread) */

static void ibio_iport_link_to_thread(struct api_thread *, struct ibio_iport *);

enum api_prim_execution_state
ibio_handle_prim_read(struct api_thread *thread, struct gseprim *read, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    gsvalue res;
    gsvalue acceptor;
    struct ibio_iport *iport;

    iport = (struct ibio_iport*)read->arguments[0];
    acceptor = read->arguments[1];

    res = 0;
    lock(&iport->lock);
    {
        if (!iport->active) {
            api_abend(thread, "read on inactive iport: %p", iport);
            unlock(&iport->lock);
            return api_st_error;
        }
        if (iport->reading) {
            api_abend_unimpl(thread, __FILE__, __LINE__, "ibio_handle_prim_read: block on previous read");
            unlock(&iport->lock);
            return api_st_error;
        }

        iport->reading = acceptor;
        iport->reading_thread = thread;
        ibio_iport_link_to_thread(thread, iport);

        res = (gsvalue)iport->position;
    }
    unlock(&iport->lock);

    *pv = res;
    return api_st_success;
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
        return gsubprim_return(thread, pos, 0, 0);
    } else {
        unlock(&seg->lock);
        return gsubprim_return(thread, pos, 1, 0);
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
    link = gs_sys_seg_suballoc(&ibio_thread_to_iport_link_descr, &ibio_thread_to_iport_link_nursury, sizeof(*link), sizeof(void*));
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

static struct gs_block_class ibio_iport_segment_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "IBIO iports",
};
static void *ibio_iport_segment_nursury;
static Lock ibio_iport_segment_lock;

static
struct ibio_iport *
ibio_alloc_iport(struct ibio_channel_segment *chan)
{
    struct ibio_iport *res;

    lock(&ibio_iport_segment_lock);
    res = gs_sys_seg_suballoc(&ibio_iport_segment_descr, &ibio_iport_segment_nursury, sizeof(*res), sizeof(void*));
    unlock(&ibio_iport_segment_lock);

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;
    res->error = 0;

    res->reading = 0;
    res->reading_thread = 0;

    res->position = chan->items;
    chan->iport = res;

    return res;
}

#define IBIO_READ_BUFFER_SIZE 0x10000

static struct gs_block_class ibio_iport_read_buffer_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "IBIO Read Buffers",
};
static void *ibio_iport_read_buffer_nursury;
static Lock ibio_iport_read_buffer_lock;

static
void
ibio_setup_iport_read_buffer(struct ibio_iport *iport)
{
    struct gs_blockdesc *nursury_block;
    void *bufbase, *buf, *bufextent;

    lock(&ibio_iport_read_buffer_lock);
    {
        buf = gs_sys_seg_suballoc(&ibio_iport_read_buffer_descr, &ibio_iport_read_buffer_nursury, 0x10, sizeof(uchar));
        nursury_block = START_OF_BLOCK(buf);
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
