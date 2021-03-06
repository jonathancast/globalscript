#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "ibio.h"

/* §section Managing write threads */

static void ibio_write_threads_up(void);
static void ibio_write_threads_down(void);

#define IBIO_NUM_WRITE_THREADS 0x100

static struct ibio_write_thread_queue {
    Lock lock;
    int refcount;
    int numthreads;
    struct ibio_oport *oports[IBIO_NUM_WRITE_THREADS];
} *ibio_write_thread_queue;

static struct gs_sys_global_block_suballoc_info ibio_write_thread_queue_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Write Thread Queue",
    },
};

struct ibio_write_thread_args {
};

static gs_sys_gc_post_callback ibio_write_thread_cleanup;
static gs_sys_gc_failure_callback ibio_write_thread_gc_failure_cleanup;

static void ibio_write_thread_main(void *);

int
ibio_write_threads_init(char *err, char *eerr)
{
    struct ibio_write_thread_args ibio_write_thread_args;
    int writepid;

    if (ibio_write_thread_queue)
        gsfatal("ibio_write_threads_init called twice")
    ;

    ibio_write_thread_queue = gs_sys_global_block_suballoc(&ibio_write_thread_queue_info, sizeof(*ibio_write_thread_queue));

    memset(ibio_write_thread_queue, 0, sizeof(*ibio_write_thread_queue));

    ibio_write_threads_up();

    api_at_termination(ibio_write_threads_down);

    gs_sys_gc_post_callback_register(ibio_write_thread_cleanup);
    gs_sys_gc_failure_callback_register(ibio_write_thread_gc_failure_cleanup);

    ace_up();

    if ((writepid = gscreate_thread_pool(ibio_write_thread_main, &ibio_write_thread_args, sizeof(ibio_write_thread_args))) < 0) {
        seprint(err, eerr, "Couldn't create IBIO write thread process pool: %r");
        ace_down();
        ibio_write_threads_down();
        return -1;
    }

    return writepid;
}

static
void
ibio_write_thread_main(void *p)
{
    int have_clients, have_threads;

    do {
        gs_sys_gc_allow_collection(0);

        lock(&ibio_write_thread_queue->lock);
        have_clients = ibio_write_thread_queue->refcount > 0;
        have_threads = ibio_write_thread_queue->numthreads > 0;
        unlock(&ibio_write_thread_queue->lock);

        sleep(1);
    } while (have_clients || have_threads);

    ace_down();
}

int
ibio_write_thread_cleanup(struct gsstringbuilder *err)
{
    int i;
    struct ibio_write_thread_queue *new_write_thread_queue;
    gsvalue gctemp;

    new_write_thread_queue = gs_sys_global_block_suballoc(&ibio_write_thread_queue_info, sizeof(*ibio_write_thread_queue));
    memcpy(new_write_thread_queue, ibio_write_thread_queue, sizeof(*ibio_write_thread_queue));
    ibio_write_thread_queue = new_write_thread_queue;

    for (i = 0; i < IBIO_NUM_WRITE_THREADS; i++) {
        struct ibio_oport *oport;
        if (oport = ibio_write_thread_queue->oports[i]) {
            if (oport->forward) {
                oport = ibio_write_thread_queue->oports[i] = ibio_write_thread_queue->oports[i]->forward;
            } else {
                gsvalue oportv;

                oportv = (gsvalue)oport;
                if (GS_GC_TRACE(err, &oportv) < 0) return -1;
                oport = ibio_write_thread_queue->oports[i] = (struct ibio_oport *)oportv;
                oport->active = 0;
            }

            if (oport->writing_thread) oport->writing_thread = api_thread_gc_forward(oport->writing_thread);
        }
    }

    return 0;
}

void
ibio_write_thread_gc_failure_cleanup()
{
    int i;
    struct ibio_oport *oport;

    for (i = 0; i < IBIO_NUM_WRITE_THREADS; i++) {
        if (oport = ibio_write_thread_queue->oports[i]) {
            if (oport->forward) ibio_write_thread_queue->oports[i] = oport = oport->forward;
            if (oport->writing_thread) oport->writing_thread = api_thread_gc_forward(oport->writing_thread);
        }
    }
}

static
void
ibio_write_threads_up()
{
    lock(&ibio_write_thread_queue->lock);
    ibio_write_thread_queue->refcount++;
    unlock(&ibio_write_thread_queue->lock);
}

static
void
ibio_write_threads_down()
{
    int i;

    lock(&ibio_write_thread_queue->lock);

    ibio_write_thread_queue->refcount--;
    if (!ibio_write_thread_queue->refcount) {
        for (i = 0; i < IBIO_NUM_WRITE_THREADS; i++) {
            if (ibio_write_thread_queue->oports[i]) {
                lock(&ibio_write_thread_queue->oports[i]->lock);
                ibio_write_thread_queue->oports[i]->active = 0;
                unlock(&ibio_write_thread_queue->oports[i]->lock);
            }
        }
    }

    unlock(&ibio_write_thread_queue->lock);
}

/* §section Opening files */

static struct ibio_oport *ibio_alloc_oport(void);
static void ibio_setup_oport_write_buffer(struct ibio_oport *);

struct ibio_write_process_args {
    struct ibio_oport *oport;
};

static void ibio_write_process_main(void *);

gsvalue
ibio_oport_fdopen(int fd, char *err, char *eerr)
{
    struct ibio_oport *res;
    struct ibio_write_process_args args;
    int writepid;

    res = ibio_alloc_oport();

    args.oport = res;

    res->fd = fd;
    ibio_setup_oport_write_buffer(res);

    unlock(&res->lock);

    ace_up();

    /* Create write process tied to res */
    if ((writepid = gscreate_thread_pool(ibio_write_process_main, &args, sizeof(args))) < 0) {
        seprint(err, eerr, "Couldn't create IBIO write process: %r");
        return 0;
    }

    return (gsvalue)res;
}

/* §section Writing (from API thread) */

static struct gs_sys_global_block_suballoc_info ibio_oport_segment_info;
static void ibio_oport_link_to_thread(struct api_thread *, struct ibio_oport *);

struct ibio_write_blocking {
    struct api_prim_blocking bl;
    gsvalue s;
    struct ibio_oport *oport;
    struct ibio_oport_write_blocker *blocking;
};

struct ibio_oport_write_blocker {
    struct ibio_oport_write_blocker *gcforward;
    struct ibio_oport_write_blocker *next;
};

static struct ibio_oport_write_blocker *ibio_oport_write_blocker_alloc(void);

static void *ibio_write_blocking_alloc(void);

enum api_prim_execution_state
ibio_handle_prim_write(struct api_thread *thread, struct gsapiprim *write, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct ibio_write_blocking *write_blocking;

    struct gs_blockdesc *block;

    if (*pblocking) {
        write_blocking = (struct ibio_write_blocking *)*pblocking;
    } else {
        gsvalue oportv;

        *pblocking = ibio_write_blocking_alloc();
        write_blocking = (struct ibio_write_blocking *)*pblocking;
        oportv = write->p.arguments[0];
        write_blocking->s = write->p.arguments[1];

        /* §c{oportv} is a WHNF by the types */
        block = BLOCK_CONTAINING(oportv);
        if (block->class != &ibio_oport_segment_info.descr) {
            api_take_thread(thread);
            api_abend(thread, "ibio_handle_prim_write: o is a %s not an oport", block->class->description);
            api_release_thread(thread);
            return api_st_error;
        }

        write_blocking->oport = (struct ibio_oport *)oportv;
        write_blocking->blocking = 0;
    }

    lock(&write_blocking->oport->lock);

    if (!write_blocking->oport->active) {
        unlock(&write_blocking->oport->lock);
        api_take_thread(thread);
        api_abend(thread, "write on inactive oport: %p", write_blocking->oport);
        api_release_thread(thread);
        return api_st_error;
    }
    if (!write_blocking->oport->writing) {
        if (write_blocking->blocking) {
            if (write_blocking->blocking == write_blocking->oport->waiting_to_write) {
                write_blocking->oport->waiting_to_write = write_blocking->oport->waiting_to_write->next;
                if (!write_blocking->oport->waiting_to_write)
                    write_blocking->oport->waiting_to_write_end = &write_blocking->oport->waiting_to_write
                ;
            } else {
                unlock(&write_blocking->oport->lock);
                api_take_thread(thread);
                api_abend_unimpl(thread, __FILE__, __LINE__, "ibio_handle_prim_write: still blocking on previous write(s)");
                api_release_thread(thread);
                return api_st_error;
            }
        }
        write_blocking->oport->writing_at = write->pos;
        write_blocking->oport->writing = write_blocking->s;
        write_blocking->oport->writing_thread = thread;
        ibio_oport_link_to_thread(thread, write_blocking->oport);

        unlock(&write_blocking->oport->lock);
        *pv = gsemptyrecord(write->pos);
        return api_st_success;
    } else if (!write_blocking->blocking) {
        write_blocking->blocking = *write_blocking->oport->waiting_to_write_end = ibio_oport_write_blocker_alloc();

        write_blocking->oport->waiting_to_write_end = &write_blocking->blocking->next;
        unlock(&write_blocking->oport->lock);
        return api_st_blocked;
    } else {
        unlock(&write_blocking->oport->lock);
        return api_st_blocked;
    }
}

static api_prim_blocking_gccopy ibio_write_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_write_blocking_gcevacuate;
static api_prim_blocking_gccleanup ibio_write_blocking_gccleanup;

void *
ibio_write_blocking_alloc()
{
    return api_blocking_alloc(sizeof(struct ibio_write_blocking), ibio_write_blocking_gccopy, ibio_write_blocking_gcevacuate, ibio_write_blocking_gccleanup);
}

struct api_prim_blocking *
ibio_write_blocking_gccopy(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    struct ibio_write_blocking *blocking, *newblocking;

    blocking = (struct ibio_write_blocking *)pblocking;

    newblocking = ibio_write_blocking_alloc();

    memcpy(newblocking, blocking, sizeof(*newblocking));

    return (struct api_prim_blocking *)newblocking;
}

static
int
ibio_write_blocking_gcevacuate(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    struct ibio_write_blocking *blocking;
    gsvalue gcv, gctemp;

    blocking = (struct ibio_write_blocking *)pblocking;

    if (GS_GC_TRACE(err, &blocking->s) < 0) return -1;

    gcv = (gsvalue)blocking->oport;
    if (GS_GC_TRACE(err, &gcv) < 0) return -1;
    blocking->oport = (struct ibio_oport *)gcv;

    if (!blocking->blocking->gcforward) {
        gsstring_builder_print(err, UNIMPL("ibio_write_blocking_gcevacuate: blocking: no forward?!"));
        return -1;
    }
    blocking->blocking = blocking->blocking->gcforward;

    return 0;
}

void
ibio_write_blocking_gccleanup(struct api_prim_blocking *blocking)
{
}

/* §section Writing to a file (write process) */

static void ibio_oport_unlink_from_thread(struct api_thread *, struct ibio_oport *);

static
void
ibio_write_process_main(void *p)
{
    struct ibio_write_process_args *args;
    int i;
    int tid;
    int active, runnable;
    int nloops;
    vlong buftime;
    gstypecode st;
    struct ibio_oport *oport;

    args = (struct ibio_write_process_args *)p;

    oport = args->oport;

    gssetprocessname("oport[%d]", oport->fd);

    tid = -1;
    lock(&ibio_write_thread_queue->lock);
    {
        if (!ibio_write_thread_queue->refcount) {
            unlock(&ibio_write_thread_queue->lock);
            lock(&oport->lock);
            oport->active = 0;
            unlock(&oport->lock);

            ace_down();
            return;
        }
        for (i = 0; i < IBIO_NUM_WRITE_THREADS; i++)
            if (!ibio_write_thread_queue->oports[i]) {
                ibio_write_thread_queue->oports[i] = oport;
                tid = i;
                ibio_write_thread_queue->numthreads++;
                break;
            }
        ;
    }
    unlock(&ibio_write_thread_queue->lock);

    if (tid < 0) {
        lock(&oport->lock);
        gswarning("%s:%d: ibio_write_process_main: out of write threads", __FILE__, __LINE__);
        oport->active = 0;
        unlock(&oport->lock);
        return;
    }

    nloops = 0;
    buftime = 0;
    do {
        struct gsstringbuilder *err;
        int gcres;

        if (buftime) nloops++;
        runnable = 1;

        err = gsreserve_string_builder();
        gcres = 0;
        if (gs_sys_gc_want_collection()) {
            if ((gcres = gs_sys_wait_for_collection_to_finish(err)) < 0) {
                gsfinish_string_builder(err);
                gswarning("GC failed: %s", err->start);
                err = gsreserve_string_builder();
            }
            if (oport->forward)
                oport = oport->forward
            ; else {
                gsstring_builder_print(err, "oport not traced in GC");
                gcres = -1;
            }
            gs_sys_gc_done_with_collection();
        }
        gsfinish_string_builder(err);
        lock(&oport->lock);
        active = oport->active || oport->writing;
        if (active && (gcres < 0 || gs_sys_memory_exhausted())) {
            if (oport->writing) {
                if (gcres < 0) {
                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "GC failed: %s", err->start);
                } else {
                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "Out of memory");
                }
                ibio_oport_unlink_from_thread(oport->writing_thread, oport);
            }
            active = oport->active = 0;
            oport->writing = oport->writing_symbol = 0;
        }
        if (oport->writing_symbol) {
            st = GS_SLOW_EVALUATE(oport->writing_at, oport->writing_symbol);
            switch (st) {
                case gstystack:
                    runnable = 0;
                    if (nloops > 1024 && nsec() - buftime > 1000 * 1000) {
                        api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: check if we should flush buffer");
                        active = oport->active = 0;
                        oport->writing = oport->writing_symbol = 0;
                        ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    }
                    break;
                case gstyindir: {
                    oport->writing_symbol = GS_REMOVE_INDIRECTION(oport->writing_at, oport->writing_symbol);
                    break;
                }
                case gstyerr: {
                    struct gserror *err;
                    char buf[0x100];

                    err = (struct gserror *)oport->writing_symbol;
                    gserror_format(buf, buf + sizeof(buf), err);
                    api_thread_post(oport->writing_thread, "write rune err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = oport->writing_symbol = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                case gstyimplerr: {
                    char buf[0x100];

                    gsimplementation_failure_format(buf, buf + sizeof(buf), (struct gsimplementation_failure *)oport->writing_symbol);
                    api_thread_post(oport->writing_thread, "write rune implementation err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = oport->writing_symbol = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                case gstyunboxed:
                    oport->bufend = gsrunetochar(oport->writing_symbol, oport->bufend, oport->bufextent);
                    if (((uchar*)oport->bufextent - (uchar*)oport->bufend) < 4) {
                        api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: flushing buffer");
                        active = oport->active = 0;
                        oport->writing = oport->writing_symbol = 0;
                        ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    } else {
                        oport->writing_symbol = 0;
                        nloops = 0;
                        buftime = nsec();
                    }
                    break;
                default:
                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: writing head state %d", st);
                    active = oport->active = 0;
                    oport->writing = oport->writing_symbol = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
            }
        } else if (oport->writing) {
            st = GS_SLOW_EVALUATE(oport->writing_at, oport->writing);
            switch (st) {
                case gstystack:
                    runnable = 0;
                    if (nloops > 1024 && nsec() - buftime > 1000 * 1000) {
                        long n = (uchar*)oport->bufend - (uchar*)oport->buf;
                        if (write(oport->fd, oport->buf, n) != n) {
                            api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: error when flushing buffer", n);
                            active = oport->active = 0;
                        }
                        oport->bufend = oport->buf;
                        nloops = 0;
                        buftime = 0;
                    }
                    break;
                case gstyindir: {
                    oport->writing = GS_REMOVE_INDIRECTION(oport->writing_at, oport->writing);
                    break;
                }
                case gstywhnf: {
                    struct gsconstr *list;

                    list = (struct gsconstr *)oport->writing;
                    switch (list->a.constrnum) {
                        case 0: { /* §gs{:} */
                            oport->writing_symbol = list->a.arguments[0];
                            oport->writing = list->a.arguments[1];
                            break;
                        }
                        case 1: { /* §gs{nil} */
                            if ((uchar*)oport->bufend > (uchar*)oport->buf) {
                                long n = (uchar*)oport->bufend - (uchar*)oport->buf;
                                if (write(oport->fd, oport->buf, n) != n) {
                                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: error when flushing buffer", n);
                                    active = oport->active = 0;
                                }
                                nloops = 0;
                                buftime = 0;
                            }
                            runnable = 0;
                            oport->writing = 0;
                            ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                            oport->bufend = oport->buf;
                            break;
                        }
                    }
                    break;
                }
                case gstyerr: {
                    struct gserror *err;
                    char buf[0x100];

                    if ((uchar*)oport->bufend > (uchar*)oport->buf) {
                        long n = (uchar*)oport->bufend - (uchar*)oport->buf;
                        if (write(oport->fd, oport->buf, n) != n) {
                            api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: error when flushing buffer", n);
                            active = oport->active = 0;
                        }
                    }

                    err = (struct gserror *)oport->writing;
                    gserror_format(buf, buf + sizeof(buf), err);
                    api_thread_post(oport->writing_thread, "write string err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                case gstyimplerr: {
                    char buf[0x100];

                    gsimplementation_failure_format(buf, buf + sizeof(buf), (struct gsimplementation_failure *)oport->writing);
                    api_thread_post(oport->writing_thread, "write string implementation err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                default:
                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: writing state %d", st);
                    active = oport->active = 0;
                    oport->writing = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
            }
        } else {
            runnable = 0;
        }
        unlock(&oport->lock);
        if (active && !runnable)
            sleep(1)
        ;
    } while (active);

    lock(&ibio_write_thread_queue->lock);
    for (i = 0; i < IBIO_NUM_WRITE_THREADS; i++)
        if (ibio_write_thread_queue->oports[i] == oport)
            ibio_write_thread_queue->oports[i] = 0
    ;
    ibio_write_thread_queue->numthreads--;
    unlock(&ibio_write_thread_queue->lock);

    ace_down();
}

/* §section Associating list of current writes to thread */

struct ibio_thread_to_oport_link {
    struct ibio_thread_to_oport_link *next;
    struct ibio_oport *oport;
};

static struct gs_sys_global_block_suballoc_info ibio_thread_to_oport_link_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Thread-to-Oport Links",
    },
};

static
void
ibio_oport_link_to_thread(struct api_thread *thread, struct ibio_oport *oport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_oport_link *link;

    api_take_thread(thread);

    data = api_thread_client_data(thread);

    link = gs_sys_global_block_suballoc(&ibio_thread_to_oport_link_info, sizeof(*link));

    link->next = data->writing_to_oport;
    link->oport = oport;
    data->writing_to_oport = link;

    api_release_thread(thread);
}

static
void
ibio_oport_unlink_from_thread(struct api_thread *thread, struct ibio_oport *oport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_oport_link **p;

    api_take_thread(thread);

    data = api_thread_client_data(thread);
    for (p = &data->writing_to_oport; *p; p = &(*p)->next) {
        if ((*p)->oport == oport) {
            *p = (*p)->next;
            api_release_thread(thread);
            return;
        }
    }

    gswarning("%s:%d: Thread %p not actually associated to oport %p", __FILE__, __LINE__, thread, oport);
    api_release_thread(thread);
}

int
ibio_thread_to_oport_link_trace(struct gsstringbuilder *err, struct ibio_thread_to_oport_link **plink)
{
    struct ibio_thread_to_oport_link *newlink;
    gsvalue gcv, gctemp;

    newlink = gs_sys_global_block_suballoc(&ibio_thread_to_oport_link_info, sizeof(*newlink));

    memcpy(newlink, *plink, sizeof(*newlink));

    *plink = newlink;

    if (newlink->next) {
        gsstring_builder_print(err, UNIMPL("ibio_thread_to_oport_link_trace: evacuate next"));
        return -1;
    }

    gcv = (gsvalue)newlink->oport;
    if (GS_GC_TRACE(err, &gcv) < 0) return -1;
    newlink->oport = (struct ibio_oport *)gcv;

    return 0;
}

void
ibio_thread_to_oport_link_cleanup(struct ibio_thread_to_oport_link **plink)
{
    struct ibio_thread_to_oport_link *link;

    for (link = *plink; link; link = link->next)
        if (link->oport->forward) link->oport = link->oport->forward
    ;
}

/* §section Allocation */

static gsvalue ibio_oport_trace(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info ibio_oport_segment_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ ibio_oport_trace,
        /* description = */ "IBIO Oports",
    },
};

static
struct ibio_oport *
ibio_alloc_oport()
{
    struct ibio_oport *res;

    res = gs_sys_global_block_suballoc(&ibio_oport_segment_info, sizeof(*res));

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;
    memset(&res->writing_at, 0, sizeof(res->writing_at));
    res->writing = res->writing_symbol = 0;
    res->waiting_to_write = 0;
    res->waiting_to_write_end = &res->waiting_to_write;
    res->writing_thread = 0;
    res->forward = 0;

    /* Output channel (linked list of segments) can be created dynamically */

    /* Put oport on list of write threads */

    return res;
}

static int ibio_oport_evacuate_buf(struct gsstringbuilder *, struct ibio_oport *);

static
gsvalue
ibio_oport_trace(struct gsstringbuilder *err, gsvalue v)
{
    struct ibio_oport *oport, *newoport;
    gsvalue gctemp;

    oport = (struct ibio_oport *)v;

    if (oport->forward) return (gsvalue)oport->forward;

    newoport = gs_sys_global_block_suballoc(&ibio_oport_segment_info, sizeof(*newoport));

    memcpy(newoport, oport, sizeof(*newoport));
    memset(&newoport->lock, 0, sizeof(newoport->lock));
    if (!newoport->waiting_to_write) newoport->waiting_to_write_end = &newoport->waiting_to_write;
    newoport->forward = 0;

    oport->forward = newoport;

    if (gs_gc_trace_pos(err, &newoport->writing_at) < 0) return 0;
    if (GS_GC_TRACE(err, &newoport->writing_symbol) < 0) return 0;
    if (GS_GC_TRACE(err, &newoport->writing) < 0) return 0;

    if (newoport->waiting_to_write) {
        struct ibio_oport_write_blocker **pblocking, *blocking, *newblocking;

        for (pblocking = &newoport->waiting_to_write; *pblocking; pblocking = &newblocking->next) {
            blocking = *pblocking;
            newblocking = ibio_oport_write_blocker_alloc();
            newblocking->next = blocking->next;
            *pblocking = blocking->gcforward = newblocking;
        }

        newoport->waiting_to_write_end = pblocking;
    }

    if (ibio_oport_evacuate_buf(err, newoport) < 0) return 0;

    return (gsvalue)newoport;
}

static struct gs_sys_global_block_suballoc_info ibio_oport_write_blocker_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Write Blocking Queue Link",
    },
};

struct ibio_oport_write_blocker *
ibio_oport_write_blocker_alloc()
{
    struct ibio_oport_write_blocker *res;

    res = gs_sys_global_block_suballoc(&ibio_oport_write_blocker_info, sizeof(struct ibio_oport_write_blocker));
    res->next = 0;
    res->gcforward = 0;

    return res;
}

#define IBIO_WRITE_BUFFER_SIZE 0x10000

static struct gs_sys_aligned_block_suballoc_info ibio_oport_write_buffer_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO Write Buffers",
    },
    /* align = */ IBIO_WRITE_BUFFER_SIZE,
};

static
void
ibio_setup_oport_write_buffer(struct ibio_oport *oport)
{
    gs_sys_aligned_block_suballoc(&ibio_oport_write_buffer_info, &oport->buf, &oport->bufextent);
    oport->bufend = oport->buf;
}

static
int
ibio_oport_evacuate_buf(struct gsstringbuilder *err, struct ibio_oport *oport)
{
    void *oldbuf, *oldbufend;

    oldbuf = oport->buf;
    oldbufend = oport->bufend;

    ibio_setup_oport_write_buffer(oport);

    if ((uchar*)oldbufend - (uchar*)oldbuf > (uchar*)oport->bufextent - (uchar*)oport->buf) {
        gsstring_builder_print(err, UNIMPL("ibio_oport_trace: buf too small for existing data"));
        return -1;
    }

    memcpy(oport->buf, oldbuf, (uchar*)oldbufend - (uchar*)oldbuf);
    oport->bufend = (uchar*)oport->buf + ((uchar*)oldbufend - (uchar*)oldbuf);

    return 0;
}
