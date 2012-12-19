/* §source.file Stat interface */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

/* §section File stat */

struct ibio_file_stat_rpc {
    struct gsrpc rpc;
    char *filename;
    struct gsbio_dir *res;
};

struct ibio_file_stat_blocking {
    struct api_prim_blocking bl;
    gsvalue s, c;
    struct gsstringbuilder fn;
    struct ibio_file_stat_rpc *rpc;
};

enum api_prim_execution_state
ibio_handle_prim_file_stat(struct api_thread *thread, struct gseprim *stat, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct api_prim_blocking *blocking;
    struct ibio_file_stat_blocking *file_stat_blocking;
    gstypecode st;

    if (blocking = *pblocking) {
        file_stat_blocking = (struct ibio_file_stat_blocking *)blocking;
    } else {
        blocking = *pblocking = api_blocking_alloc(sizeof(struct ibio_file_stat_blocking));
        file_stat_blocking = (struct ibio_file_stat_blocking *)blocking;
        file_stat_blocking->s = stat->arguments[0];
        file_stat_blocking->c = 0;
        file_stat_blocking->fn = gsreserve_string_builder();
        file_stat_blocking->rpc = 0;
    }
    for (;;) {
        if (file_stat_blocking->c) {
            st = GS_SLOW_EVALUATE(file_stat_blocking->c);

            switch (st) {
                case gstyunboxed:
                    if (gsextend_string_builder(&file_stat_blocking->fn, 4) < 0) {
                        api_abend(thread, "%P: OOM Saving file name", stat->pos);
                        return api_st_error;
                    }
                    file_stat_blocking->fn.end = gsrunetochar(file_stat_blocking->c, file_stat_blocking->fn.end, file_stat_blocking->fn.extent);
                    file_stat_blocking->c = 0;
                    break;
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle c st %d"), stat->pos, st);
                    return api_st_error;
            }
        } else if (file_stat_blocking->s) {
            st = GS_SLOW_EVALUATE(file_stat_blocking->s);

            switch (st) {
                case gstystack:
                    return api_st_blocked;
                case gstywhnf: {
                    struct gsconstr *constr;

                    constr = (struct gsconstr *)file_stat_blocking->s;
                    switch (constr->constrnum) {
                        case 0:
                            file_stat_blocking->c = constr->arguments[0];
                            file_stat_blocking->s = constr->arguments[1];
                            break;
                        case 1:
                            gsfinish_string_builder(&file_stat_blocking->fn);
                            file_stat_blocking->s = 0;
                            break;
                        default:
                            api_abend(thread, UNIMPL("%P: %P: ibio_handle_prim_file_stat: handle s constr %d"), stat->pos, constr->pos, constr->constrnum);
                            return api_st_error;
                    }
                    break;
                }
                case gstyindir:
                    file_stat_blocking->s = GS_REMOVE_INDIRECTION(file_stat_blocking->s);
                    break;
                case gstyerr: {
                    char err[0x100];

                    gserror_format(err, err + sizeof(err), (struct gserror *)file_stat_blocking->s);
                    api_abend(thread, "%P: Error evaluating file name: %s", stat->pos, err);
                    return api_st_error;
                }
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle s st %d"), stat->pos, st);
                    return api_st_error;
            }
        } else if (!file_stat_blocking->rpc) {
            struct gsrpc *rpc;
            struct ibio_file_stat_rpc *statrpc;

            rpc = gsqueue_rpc_alloc(sizeof(struct ibio_file_stat_rpc));
            file_stat_blocking->rpc = statrpc = (struct ibio_file_stat_rpc *)rpc;

            rpc->tag = ibio_uxproc_rpc_stat;
            statrpc->filename = file_stat_blocking->fn.start;
            api_send_rpc(thread, rpc);
        } else {
            struct ibio_file_stat_rpc *statrpc = (struct ibio_file_stat_rpc *)file_stat_blocking->rpc;
            int status;

            lock(&statrpc->rpc.lock);
                status = statrpc->rpc.status;
            unlock(&statrpc->rpc.lock);

            switch (status) {
                case gsrpc_failed:
                    api_abend(thread, "%P: %s", stat->pos, statrpc->rpc.err);
                    return api_st_error;
                case gsrpc_pending:
                    return api_st_blocked;
                case gsrpc_succeeded: {
                    *pv = ibio_parse_gsbio_dir(stat->pos, statrpc->res);
                    return api_st_success;
                }
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: rpc status %d"), stat->pos, status);
                    return api_st_error;
            }
        }
    }
}

gsvalue
ibio_parse_gsbio_dir(struct gspos pos, struct gsbio_dir *dir)
{
    gsvalue fields[ibio_stat_num_fields];

    fields[ibio_stat_mode_directory] = dir->d.mode & DMDIR ? gstrue(pos) : gsfalse(pos);

    return gsrecordv(pos, ibio_stat_num_fields, fields);
}

void
ibio_main_process_handle_rpc_stat(struct gsrpc *rpc)
{
    struct ibio_file_stat_rpc *statrpc;
    struct gsbio_dir *dir;
    struct gsstringbuilder err;

    statrpc = (struct ibio_file_stat_rpc *)rpc;

    dir = gsbio_stat(statrpc->filename);
    if (!dir) {
        err = gsreserve_string_builder();
        err.end = seprint(err.end, err.extent, "%r");
        gsfinish_string_builder(&err);
        rpc->status = gsrpc_failed;
        rpc->err = err.start;
        unlock(&rpc->lock);
        return;
    }

    rpc->status = gsrpc_succeeded;
    statrpc->res = dir;
    unlock(&rpc->lock);
}
