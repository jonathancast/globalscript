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

int
ibio_gsstring_eval_advance(struct api_thread *thread, struct gspos pos, struct ibio_gsstring_eval *eval)
{
    gstypecode st;

    if (eval->gsc) {
        st = GS_SLOW_EVALUATE(eval->gsc);

        switch (st) {
            case gstyunboxed:
                if (gsextend_string_builder(&eval->sb, 4) < 0) {
                    api_abend(thread, "%P: OOM Saving file name", pos);
                    return 0;
                }
                eval->sb.end = gsrunetochar(eval->gsc, eval->sb.end, eval->sb.extent);
                eval->gsc = 0;
                break;
            default:
                api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle c st %d"), pos, st);
                return 0;
        }
    } else if (eval->gss) {
        st = GS_SLOW_EVALUATE(eval->gss);

        switch (st) {
            case gstystack:
                return 0;
            case gstywhnf: {
                struct gsconstr *constr;

                constr = (struct gsconstr *)eval->gss;
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
                        api_abend(thread, UNIMPL("%P: %P: ibio_handle_prim_file_stat: handle s constr %d"), pos, constr->pos, constr->constrnum);
                        return 0;
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
                return 0;
            }
            default:
                api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: handle s st %d"), pos, st);
                return 0;
        }
    }

    return 1;
}
