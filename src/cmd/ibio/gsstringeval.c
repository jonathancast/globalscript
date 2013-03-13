/* §source.file Turning GS Strings into C Strings */

#include <u.h>
#include <libc.h>
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
        st = GS_SLOW_EVALUATE(eval->gsc);

        switch (st) {
            case gstyindir:
                eval->gsc = GS_REMOVE_INDIRECTION(eval->gsc);
                break;
            case gstyunboxed:
                if (gsextend_string_builder(&eval->sb, 4) < 0) {
                    api_abend(thread, "%P: OOM saving file name", pos);
                    return ibio_gsstring_eval_error;
                }
                eval->sb.end = gsrunetochar(eval->gsc, eval->sb.end, eval->sb.extent);
                eval->gsc = 0;
                break;
            default:
                api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle c st %d"), pos, st);
                return ibio_gsstring_eval_error;
        }
    } else if (eval->gss) {
        st = GS_SLOW_EVALUATE(eval->gss);

        switch (st) {
            case gstystack:
                return ibio_gsstring_eval_blocked;
            case gstywhnf: {
                struct gsconstr_args *constr;

                constr = (struct gsconstr_args *)eval->gss;
                switch (constr->constrnum) {
                    case 0:
                        eval->gsc = constr->arguments[0];
                        eval->gss = constr->arguments[1];
                        break;
                    case 1:
                        gsfinish_string_builder(&eval->sb);
                        eval->gss = 0;
                        break;
                    default:
                        api_abend(thread, UNIMPL("%P: %P: ibio_handle_prim_file_stat: handle s constr %d"), pos, constr->c.pos, constr->constrnum);
                        return ibio_gsstring_eval_error;
                }
                break;
            }
            case gstyindir:
                eval->gss = GS_REMOVE_INDIRECTION(eval->gss);
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
