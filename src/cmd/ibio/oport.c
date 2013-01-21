#include <u.h>
#include <libc.h>
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

static struct gs_block_class ibio_write_thread_queue_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "IBIO Write Thread Queue",
};
static void *ibio_write_thread_queue_nursury;

struct ibio_write_thread_args {
};

static void ibio_write_thread_main(void *);

int
ibio_write_threads_init(char *err, char *eerr)
{
    struct ibio_write_thread_args ibio_write_thread_args;
    int writepid;

    if (ibio_write_thread_queue)
        gsfatal("ibio_write_threads_init called twice")
    ;

    ibio_write_thread_queue = gs_sys_seg_suballoc(&ibio_write_thread_queue_descr, &ibio_write_thread_queue_nursury, sizeof(*ibio_write_thread_queue), sizeof(void*));

    memset(ibio_write_thread_queue, 0, sizeof(*ibio_write_thread_queue));

    ibio_write_threads_up();

    api_at_termination(ibio_write_threads_down);

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
        lock(&ibio_write_thread_queue->lock);
        have_clients = ibio_write_thread_queue->refcount > 0;
        have_threads = ibio_write_thread_queue->numthreads > 0;
        unlock(&ibio_write_thread_queue->lock);
    } while (have_clients || have_threads);

    ace_down();
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

static struct gs_block_class ibio_oport_segment_descr;
static void ibio_oport_link_to_thread(struct api_thread *, struct ibio_oport *);

enum api_prim_execution_state
ibio_handle_prim_write(struct api_thread *thread, struct gseprim *write, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    gsvalue oportv, s;
    struct ibio_oport *oport;
    struct gs_blockdesc *block;

    oportv = write->arguments[0];
    s = write->arguments[1];

    /* §c{oportv} is a WHNF by the types */
    block = BLOCK_CONTAINING(oportv);
    if (block->class != &ibio_oport_segment_descr) {
        api_abend(thread, "ibio_handle_prim_write: o is a %s not an oport", block->class->description);
        return api_st_error;
    }

    oport = (struct ibio_oport *)oportv;

    lock(&oport->lock);
    {
        if (!oport->active) {
            api_abend(thread, "write on inactive oport: %p", oport);
            unlock(&oport->lock);
            return api_st_error;
        }
        if (oport->writing) {
            api_abend_unimpl(thread, __FILE__, __LINE__, "ibio_handle_prim_write: block on previous write next");
            unlock(&oport->lock);
            return api_st_error;
        }

        oport->writing = s;
        oport->writing_thread = thread;
        ibio_oport_link_to_thread(thread, oport);
    }
    unlock(&oport->lock);
    *pv = gsemptyrecord(write->pos);
    return api_st_success;
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
    gstypecode st;
    struct ibio_oport *oport;
    gsvalue c;

    args = (struct ibio_write_process_args *)p;

    oport = args->oport;

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

    c = 0;
    do {
        lock(&oport->lock);
        active = oport->active || oport->writing;
        runnable = 1;
        if (c) {
            st = GS_SLOW_EVALUATE(c);
            switch (st) {
                case gstystack:
                    runnable = 0;
                    break;
                case gstyindir: {
                    c = GS_REMOVE_INDIRECTION(c);
                    break;
                }
                case gstyerr: {
                    struct gserror *err;
                    char buf[0x100];

                    err = (struct gserror *)c;
                    gserror_format(buf, buf + sizeof(buf), err);
                    api_thread_post(oport->writing_thread, "write rune err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                case gstyimplerr: {
                    char buf[0x100];

                    gsimplementation_failure_format(buf, buf + sizeof(buf), (struct gsimplementation_failure *)c);
                    api_thread_post(oport->writing_thread, "write rune implementation err: %s", buf);

                    active = oport->active = 0;
                    oport->writing = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
                }
                case gstyunboxed:
                    oport->bufend = gsrunetochar(c, oport->bufend, oport->bufextent);
                    if (((uchar*)oport->bufextent - (uchar*)oport->bufend) < 4) {
                        api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: flushing buffer");
                        active = oport->active = 0;
                        oport->writing = c = 0;
                        ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    } else {
                        c = 0;
                    }
                    break;
                default:
                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: writing head state %d", st);
                    active = oport->active = 0;
                    oport->writing = c = 0;
                    ibio_oport_unlink_from_thread(oport->writing_thread, oport);
                    break;
            }
        } else if (oport->writing) {
            st = GS_SLOW_EVALUATE(oport->writing);
            switch (st) {
                case gstystack:
                    runnable = 0;
                    break;
                case gstyindir: {
                    oport->writing = GS_REMOVE_INDIRECTION(oport->writing);
                    break;
                }
                case gstywhnf: {
                    struct gsconstr *list;

                    list = (struct gsconstr *)oport->writing;
                    switch (list->constrnum) {
                        case 0: { /* §gs{:} */
                            c = list->arguments[0];
                            oport->writing = list->arguments[1];
                            break;
                        }
                        case 1: { /* §gs{nil} */
                            if ((uchar*)oport->bufend > (uchar*)oport->buf) {
                                long n = (uchar*)oport->bufend - (uchar*)oport->buf;
                                if (write(oport->fd, oport->buf, n) != n) {
                                    api_thread_post_unimpl(oport->writing_thread, __FILE__, __LINE__, "ibio_write_process_main: error when flushing buffer", n);
                                    active = oport->active = 0;
                                }
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

static struct gs_block_class ibio_thread_to_oport_link_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "IBIO Oports",
};
static void *ibio_thread_to_oport_link_nursury;
static Lock ibio_thread_to_oport_link_lock;

static
void
ibio_oport_link_to_thread(struct api_thread *thread, struct ibio_oport *oport)
{
    struct ibio_thread_data *data;
    struct ibio_thread_to_oport_link *link;

    data = api_thread_client_data(thread);

    lock(&ibio_thread_to_oport_link_lock);
    link = gs_sys_seg_suballoc(&ibio_thread_to_oport_link_descr, &ibio_thread_to_oport_link_nursury, sizeof(*link), sizeof(void*));
    unlock(&ibio_thread_to_oport_link_lock);

    link->next = data->writing_to_oport;
    link->oport = oport;
    data->writing_to_oport = link;
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

/* §section Allocation */

static struct gs_block_class ibio_oport_segment_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "IBIO Oports",
};
static void *ibio_oport_segment_nursury;
static Lock ibio_oport_segment_lock;

static
struct ibio_oport *
ibio_alloc_oport()
{
    struct ibio_oport *res;

    lock(&ibio_oport_segment_lock);
    res = gs_sys_seg_suballoc(&ibio_oport_segment_descr, &ibio_oport_segment_nursury, sizeof(*res), sizeof(void*));
    unlock(&ibio_oport_segment_lock);

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;
    res->writing = 0;

    /* Output channel (linked list of segments) can be created dynamically */

    /* Put oport on list of write threads */

    return res;
}

#define IBIO_WRITE_BUFFER_SIZE 0x10000

static struct gs_block_class ibio_oport_write_buffer_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "IBIO Write Buffers",
};
static void *ibio_oport_write_buffer_nursury;
static Lock ibio_oport_write_buffer_lock;

static
void
ibio_setup_oport_write_buffer(struct ibio_oport *oport)
{
    struct gs_blockdesc *nursury_block;
    void *bufbase, *buf, *bufextent;

    lock(&ibio_oport_write_buffer_lock);
    {
        buf = gs_sys_seg_suballoc(&ibio_oport_write_buffer_descr, &ibio_oport_write_buffer_nursury, 0x10, sizeof(uchar));
        nursury_block = START_OF_BLOCK(buf);
        bufbase = (uchar*)buf;
        if ((uintptr)bufbase % IBIO_WRITE_BUFFER_SIZE)
            bufbase = (uchar*)bufbase - ((uintptr)bufbase % IBIO_WRITE_BUFFER_SIZE)
        ;
        bufextent = (uchar*)bufbase + IBIO_WRITE_BUFFER_SIZE;
        if ((uchar*)bufextent >= (uchar*)END_OF_BLOCK(nursury_block))
            ibio_oport_write_buffer_nursury = 0
        ; else
            ibio_oport_write_buffer_nursury = bufextent
        ;
    }
    unlock(&ibio_oport_write_buffer_lock);

    oport->buf = buf;
    oport->bufend = buf;
    oport->bufextent = bufextent;
}
