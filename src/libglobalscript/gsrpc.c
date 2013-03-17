#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

struct gsrpc_queue {
    Lock lock;
    int refcount;
    struct gsrpc_queue_link *head, **tail;
    void *nursury;
};

struct gs_sys_global_block_suballoc_info gsrpc_queue_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "RPC queue",
    },
};

struct gsrpc_queue *
gsqueue_alloc()
{
    struct gsrpc_queue *res;

    res = gs_sys_global_block_suballoc(&gsrpc_queue_info, sizeof(*res));

    memset(res, 0, sizeof(*res));

    res->refcount = 1;

    res->tail = &res->head;

    return res;
}

void
gsqueue_down(struct gsrpc_queue *q)
{
    lock(&q->lock);

    q->refcount--;

    unlock(&q->lock);
}

static struct gs_sys_global_block_suballoc_info gsrpc_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "RPC",
    },
};

struct gsrpc *
gsqueue_rpc_alloc(ulong sz)
{
    struct gsrpc *res;

    res = gs_sys_global_block_suballoc(&gsrpc_info, sz);

    memset(res, 0, sz);

    return res;
}

struct gsrpc_queue_link {
    struct gsrpc *rpc;
    struct gsrpc_queue_link *next;
};

struct gsrpc *
gsqueue_get_rpc(struct gsrpc_queue *q)
{
    struct gsrpc *res;

    res = 0;

    while (!res) {
        lock(&q->lock);
        if (!q->refcount) {
            unlock(&q->lock);
            return 0;
        }
        if (q->head) {
            res = q->head->rpc;
            q->head = q->head->next;
            if (!q->head)
                q->tail = &q->head
            ;
            unlock(&q->lock);
        } else {
            unlock(&q->lock);
            sleep(1);
        }
    }

    lock(&res->lock);
    return res;
}

static struct gs_block_class gsrpc_queue_link_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "RPC Queue Link",
};

void
gsqueue_send_rpc(struct gsrpc_queue *q, struct gsrpc *rpc)
{
    struct gsrpc_queue_link *link;

    lock(&q->lock);
    *q->tail = link = gs_sys_block_suballoc(&gsrpc_queue_link_descr, &q->nursury, sizeof(**q->tail), sizeof(Lock));
    link->rpc = rpc;
    link->next = 0;
    q->tail = &link->next;
    unlock(&q->lock);
}

