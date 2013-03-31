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
    char *filename;
    gsvalue iport;
};

static api_prim_blocking_gccopy ibio_file_read_open_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_file_read_open_blocking_gcevacuate;

enum api_prim_execution_state
ibio_handle_prim_file_read_open(struct api_thread *thread, struct gseprim *open, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking;

    if (*pblocking) {
        file_read_open_blocking = (struct ibio_file_read_open_blocking *)*pblocking;
    } else {
        *pblocking = api_blocking_alloc(sizeof(struct ibio_file_read_open_blocking), ibio_file_read_open_blocking_gccopy, ibio_file_read_open_blocking_gcevacuate);
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

            rpc = gsqueue_rpc_alloc(sizeof(struct ibio_file_read_open_rpc));
            file_read_open_blocking->rpc = openrpc = (struct ibio_file_read_open_rpc *)rpc;

            rpc->tag = ibio_uxproc_rpc_file_read_open;
            openrpc->pos = open->pos;
            openrpc->io = file_read_open_blocking->io;
            openrpc->filename = file_read_open_blocking->fn.sb.start;
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
ibio_file_read_open_blocking_gccopy(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_file_read_open_blocking_gccopy"));
    return 0;
}

static
int
ibio_file_read_open_blocking_gcevacuate(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_file_read_open_blocking_gcevacuate"));
    return -1;
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

    uxio = gsisdir(openrpc->filename) ? ibio_dir_uxio(openrpc->filename) : ibio_file_uxio();

    if ((fd = open(openrpc->filename, OREAD)) < 0) {
        ibio_rpc_fail(rpc, "%P: %s: %r", openrpc->pos, openrpc->filename, openrpc->pos);
        return;
    }

    if (!(res = ibio_iport_fdopen(fd, uxio, openrpc->io, errbuf, errbuf + sizeof(errbuf)))) {
        ibio_rpc_fail(rpc, "%P: %s: %s", openrpc->pos, openrpc->filename, errbuf);
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
    struct gsstringbuilder err;
    va_list arg;

    err = gsreserve_string_builder();

    va_start(arg, fmt);
    gsstring_builder_vprint(&err, fmt, arg);
    va_end(arg);

    gsfinish_string_builder(&err);

    rpc->status = gsrpc_failed;
    rpc->err = err.start;
    unlock(&rpc->lock);
}
