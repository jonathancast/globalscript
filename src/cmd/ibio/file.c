/* §source.file Files */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

struct ibio_file_read_open_blocking {
    struct api_prim_blocking bl;
    struct ibio_external_io *io;
    struct ibio_gsstring_eval fn;
    struct ibio_file_read_open_rpc *rpc;
};

struct ibio_file_read_open_rpc {
    struct gsrpc rpc;
    struct gspos pos;
    struct ibio_external_io *io;
    struct gsstringbuilder *filename;
    gsvalue iport;
};

static api_prim_blocking_gccopy ibio_file_read_open_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_file_read_open_blocking_gcevacuate;
static api_prim_blocking_gccleanup ibio_file_read_open_blocking_gccleanup;

static gsrpc_gccopy ibio_file_read_open_rpc_gccopy;

enum api_prim_execution_state
ibio_handle_prim_file_read_open(struct api_thread *thread, struct gseprim *open, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking;

    if (*pblocking) {
        file_read_open_blocking = (struct ibio_file_read_open_blocking *)*pblocking;
    } else {
        *pblocking = api_blocking_alloc(sizeof(struct ibio_file_read_open_blocking), ibio_file_read_open_blocking_gccopy, ibio_file_read_open_blocking_gcevacuate, ibio_file_read_open_blocking_gccleanup);
        file_read_open_blocking = (struct ibio_file_read_open_blocking *)*pblocking;
        file_read_open_blocking->io = (struct ibio_external_io *)open->p.arguments[0];
        ibio_gsstring_eval_start(&file_read_open_blocking->fn, open->p.arguments[1]);
        file_read_open_blocking->rpc = 0;
    }
    for (;;) {
        if (file_read_open_blocking->fn.gss) {
            enum ibio_gsstring_eval_state st;

            st = ibio_gsstring_eval_advance(thread, open->pos, &file_read_open_blocking->fn);
            switch (st) {
                case ibio_gsstring_eval_error:
                    return api_st_error;
                case ibio_gsstring_eval_blocked:
                    return api_st_blocked;
                case ibio_gsstring_eval_success:
                    break;
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_read_open: ibio_gsstring_eval_advance st %d"), open->pos, st);
                    return api_st_error;
            }
        } else if (!file_read_open_blocking->rpc) {
            struct gsrpc *rpc;
            struct ibio_file_read_open_rpc *openrpc;

            rpc = gsqueue_rpc_alloc(sizeof(struct ibio_file_read_open_rpc), ibio_file_read_open_rpc_gccopy);
            file_read_open_blocking->rpc = openrpc = (struct ibio_file_read_open_rpc *)rpc;

            rpc->tag = ibio_uxproc_rpc_file_read_open;
            openrpc->pos = open->pos;
            openrpc->io = file_read_open_blocking->io;
            openrpc->filename = file_read_open_blocking->fn.sb;
            api_send_rpc(thread, rpc);
        } else {
            struct ibio_file_read_open_rpc *openrpc = (struct ibio_file_read_open_rpc *)file_read_open_blocking->rpc;
            int status;

            lock(&openrpc->rpc.lock);
                status = openrpc->rpc.status;
            unlock(&openrpc->rpc.lock);

            switch (status) {
                case gsrpc_failed:
                    api_abend(thread, "%s", openrpc->rpc.err);
                    return api_st_error;
                case gsrpc_pending:
                    return api_st_blocked;
                case gsrpc_succeeded: {
                    *pv = openrpc->iport;
                    return api_st_success;
                }
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: rpc status %d"), open->pos, status);
                    return api_st_error;
            }
        }
    }
}

static
struct api_prim_blocking *
ibio_file_read_open_blocking_gccopy(struct gsstringbuilder *err, struct api_prim_blocking *blocking)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking, *new_file_read_open_blocking;
    struct api_prim_blocking *newblocking;

    file_read_open_blocking = (struct ibio_file_read_open_blocking *)blocking;

    newblocking = api_blocking_alloc(sizeof(struct ibio_file_read_open_blocking), ibio_file_read_open_blocking_gccopy, ibio_file_read_open_blocking_gcevacuate, ibio_file_read_open_blocking_gccleanup);
    new_file_read_open_blocking = (struct ibio_file_read_open_blocking *)newblocking;

    memcpy(new_file_read_open_blocking, file_read_open_blocking, sizeof(struct ibio_file_read_open_blocking));

    return newblocking;
}

static
int
ibio_file_read_open_blocking_gcevacuate(struct gsstringbuilder *err, struct api_prim_blocking *blocking)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking;

    file_read_open_blocking = (struct ibio_file_read_open_blocking *)blocking;
    if (ibio_external_io_trace(err, &file_read_open_blocking->io) < 0) return -1;
    if (ibio_gsstring_eval_evacuate(err, &file_read_open_blocking->fn) < 0) return -1;

    if (file_read_open_blocking->rpc && gs_sys_block_in_gc_from_space(file_read_open_blocking->rpc)) {
        struct gsrpc *rpc;
        rpc = (struct gsrpc *)file_read_open_blocking->rpc;
        if (gsqueue_rpc_gc_trace(err, &rpc) < 0) return -1;
        file_read_open_blocking->rpc = (struct ibio_file_read_open_rpc *)rpc;
    }

    return 0;
}

void
ibio_file_read_open_blocking_gccleanup(struct api_prim_blocking *blocking)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking;

    file_read_open_blocking = (struct ibio_file_read_open_blocking *)blocking;

    if (file_read_open_blocking->rpc && file_read_open_blocking->rpc->rpc.forward)
        file_read_open_blocking->rpc = (struct ibio_file_read_open_rpc *)file_read_open_blocking->rpc->rpc.forward
    ;
}

struct gsrpc *
ibio_file_read_open_rpc_gccopy(struct gsstringbuilder *err, struct gsrpc *gsrpc)
{
    gsstring_builder_print(err, UNIMPL("ibio_file_read_open_rpc_gccopy"));
    return 0;
}

static void ibio_rpc_fail(struct gsrpc *, char *, ...);

void
ibio_main_process_handle_rpc_file_read_open(struct gsrpc *rpc)
{
    struct ibio_file_read_open_rpc *openrpc;
    char errbuf[0x100];
    gsvalue res;
    int fd;
    struct ibio_uxio *uxio;

    openrpc = (struct ibio_file_read_open_rpc *)rpc;

    uxio = gsisdir(openrpc->filename->start) ? ibio_dir_uxio(openrpc->filename->start) : ibio_file_uxio();

    if ((fd = open(openrpc->filename->start, OREAD)) < 0) {
        ibio_rpc_fail(rpc, "%P: %s: %r", openrpc->pos, openrpc->filename->start, openrpc->pos);
        return;
    }

    if (!(res = ibio_iport_fdopen(fd, uxio, openrpc->io, errbuf, errbuf + sizeof(errbuf)))) {
        ibio_rpc_fail(rpc, "%P: %s: %s", openrpc->pos, openrpc->filename->start, errbuf);
        return;
    }

    rpc->status = gsrpc_succeeded;
    openrpc->iport = res;
    unlock(&rpc->lock);
}

static
void
ibio_rpc_fail(struct gsrpc *rpc, char *fmt, ...)
{
    struct gsstringbuilder *err;
    va_list arg;

    err = gsreserve_string_builder();

    va_start(arg, fmt);
    gsstring_builder_vprint(err, fmt, arg);
    va_end(arg);

    gsfinish_string_builder(err);

    rpc->status = gsrpc_failed;
    rpc->err = err;
    unlock(&rpc->lock);
}
