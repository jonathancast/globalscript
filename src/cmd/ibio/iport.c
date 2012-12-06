#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

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

static struct ibio_iport *ibio_alloc_iport(void);

struct ibio_read_process_args {
    struct ibio_iport *iport;
};

static void ibio_read_process_main(void *);

gsvalue
ibio_iport_fdopen(int fd, char *err, char *eerr)
{
    struct ibio_iport *res;
    struct ibio_read_process_args args;
    int readpid;

    res = ibio_alloc_iport();

    args.iport = res;

    res->fd = fd;

    unlock(&res->lock);

    /* Create read process tied to res */
    if ((readpid = gscreate_thread_pool(ibio_read_process_main, &args, sizeof(args))) < 0) {
        seprint(err, eerr, "Couldn't create IBIO read process: %r");
        return 0;
    }

    return (gsvalue)res;
}

/* §section Reading from a file (read process) */

static
void
ibio_read_process_main(void *p)
{
    struct ibio_read_process_args *args;
    int i;
    int tid;
    int active, runnable;
    struct ibio_iport *iport;

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

    do {
        lock(&iport->lock);
        active = iport->active;
        runnable = 0;

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
ibio_alloc_iport()
{
    struct ibio_iport *res;

    lock(&ibio_iport_segment_lock);
    res = gs_sys_seg_suballoc(&ibio_iport_segment_descr, &ibio_iport_segment_nursury, sizeof(*res), sizeof(void*));
    unlock(&ibio_iport_segment_lock);

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);
    res->active = 1;

    /* Output channel (linked list of segments) can be created dynamically */

    /* Put iport on list of read threads */

    return res;
}
