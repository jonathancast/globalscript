/* §source.file Turning GS Strings into C Strings */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "ibio.h"

void
ibio_gsstring_eval_start(struct ibio_gsstring_eval *eval, gsvalue gss)
{
    eval->gss = gss;
    eval->gsc = 0;
    eval->sb = gsreserve_string_builder();
}

enum ibio_gsstring_eval_state
ibio_gsstring_eval_advance(struct api_thread *thread, struct gspos pos, struct ibio_gsstring_eval *eval)
{
    gstypecode st;

    if (eval->gsc) {
        st = GS_SLOW_EVALUATE(pos, eval->gsc);

        switch (st) {
            case gstyindir:
                eval->gsc = GS_REMOVE_INDIRECTION(pos, eval->gsc);
                break;
            case gstyunboxed:
                if (gsextend_string_builder(eval->sb, 4) < 0) {
                    api_abend(thread, "%P: OOM saving file name", pos);
                    return ibio_gsstring_eval_error;
                }
                eval->sb->end = gsrunetochar(eval->gsc, eval->sb->end, END_OF_BLOCK(BLOCK_CONTAINING(eval->sb)));
                eval->gsc = 0;
                break;
            case gstyimplerr: {
                char err[0x100];

                gsimplementation_failure_format(err, err + sizeof(err), (struct gsimplementation_failure *)eval->gsc);
                api_abend(thread, "%P: %s", pos, err);
                return ibio_gsstring_eval_error;
            }
            default:
                api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle c st %d"), pos, st);
                return ibio_gsstring_eval_error;
        }
    } else if (eval->gss) {
        st = GS_SLOW_EVALUATE(pos, eval->gss);

        switch (st) {
            case gstystack:
                return ibio_gsstring_eval_blocked;
            case gstywhnf: {
                struct gsconstr *constr;

                constr = (struct gsconstr *)eval->gss;
                switch (constr->a.constrnum) {
                    case 0:
                        eval->gsc = constr->a.arguments[0];
                        eval->gss = constr->a.arguments[1];
                        break;
                    case 1:
                        gsfinish_string_builder(eval->sb);
                        eval->gss = 0;
                        break;
                    default:
                        api_abend(thread, UNIMPL("%P: %P: ibio_handle_prim_file_stat: handle s constr %d"), pos, constr->pos, constr->a.constrnum);
                        return ibio_gsstring_eval_error;
                }
                break;
            }
            case gstyindir:
                eval->gss = GS_REMOVE_INDIRECTION(pos, eval->gss);
                break;
            case gstyerr: {
                char err[0x100];

                gserror_format(err, err + sizeof(err), (struct gserror *)eval->gss);
                api_abend(thread, "%P: Error evaluating file name: %s", pos, err);
                return ibio_gsstring_eval_error;
            }
            default:
                api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle s st %d"), pos, st);
                return ibio_gsstring_eval_error;
        }
    }

    return ibio_gsstring_eval_success;
}

int
ibio_gsstring_eval_evacuate(struct gsstringbuilder *err, struct ibio_gsstring_eval *eval)
{
    gsvalue gctemp;

    if (GS_GC_TRACE(err, &eval->gss) < 0) return -1;
    if (GS_GC_TRACE(err, &eval->gsc) < 0) return -1;
    if (gs_sys_block_in_gc_from_space(eval->sb) && gsstring_builder_trace(err, &eval->sb) < 0) return -1;

    return 0;
}
