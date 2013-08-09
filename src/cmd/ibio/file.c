/* §source.file Files */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

struct ibio_file_read_open_blocking {
    struct api_prim_blocking bl;
    struct ibio_external_io *io;
    struct ibio_gsstring_eval fn;
};

static api_prim_blocking_gccopy ibio_file_read_open_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_file_read_open_blocking_gcevacuate;
static api_prim_blocking_gccleanup ibio_file_read_open_blocking_gccleanup;

enum api_prim_execution_state
ibio_handle_prim_file_read_open(struct api_thread *thread, struct gseprim *gsopen, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct ibio_file_read_open_blocking *file_read_open_blocking;

    if (*pblocking) {
        file_read_open_blocking = (struct ibio_file_read_open_blocking *)*pblocking;
    } else {
        *pblocking = api_blocking_alloc(sizeof(struct ibio_file_read_open_blocking), ibio_file_read_open_blocking_gccopy, ibio_file_read_open_blocking_gcevacuate, ibio_file_read_open_blocking_gccleanup);
        file_read_open_blocking = (struct ibio_file_read_open_blocking *)*pblocking;
        file_read_open_blocking->io = (struct ibio_external_io *)gsopen->p.arguments[0];
        ibio_gsstring_eval_start(&file_read_open_blocking->fn, gsopen->p.arguments[1]);
    }
    for (;;) {
        if (file_read_open_blocking->fn.gss) {
            enum ibio_gsstring_eval_state st;

            st = ibio_gsstring_eval_advance(thread, gsopen->pos, &file_read_open_blocking->fn);
            switch (st) {
                case ibio_gsstring_eval_error:
                    return api_st_error;
                case ibio_gsstring_eval_blocked:
                    return api_st_blocked;
                case ibio_gsstring_eval_success:
                    break;
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_read_open: ibio_gsstring_eval_advance st %d"), gsopen->pos, st);
                    return api_st_error;
            }
        } else {
            char errbuf[0x100];
            gsvalue res;
            int fd;
            struct ibio_uxio *uxio;

            uxio = gsisdir(file_read_open_blocking->fn.sb->start) ? ibio_dir_uxio(file_read_open_blocking->fn.sb->start) : ibio_file_uxio();

            if ((fd = open(file_read_open_blocking->fn.sb->start, OREAD)) < 0) {
                struct gsstringbuilder *err;
                gsvalue gserr;

                err = gsreserve_string_builder();
                gsstring_builder_print(err, "%P: %s: %r", gsopen->pos, file_read_open_blocking->fn.sb->start, gsopen->pos);
                gsfinish_string_builder(err);

                gserr = gscstringtogsstring(gsopen->pos, err->start);
                *pv = gsconstr(gsopen->pos, 0, 1, gserr);
                return api_st_success;
            }

            if (!(res = ibio_iport_fdopen(fd, uxio, file_read_open_blocking->io, errbuf, errbuf + sizeof(errbuf)))) {
                struct gsstringbuilder *err;
                gsvalue gserr;

                err = gsreserve_string_builder();
                gsstring_builder_print(err, "%P: %s: %s", gsopen->pos, file_read_open_blocking->fn.sb->start, errbuf);
                gsfinish_string_builder(err);

                gserr = gscstringtogsstring(gsopen->pos, err->start);
                *pv = gsconstr(gsopen->pos, 0, 1, gserr);
                return api_st_success;
            }

            *pv = gsconstr(gsopen->pos, 1, 1, res);
            return api_st_success;
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

    return 0;
}

void
ibio_file_read_open_blocking_gccleanup(struct api_prim_blocking *blocking)
{
}
