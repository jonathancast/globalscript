/* §source.file Stat interface */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"
#include "stat.h"

/* §section File stat */

struct ibio_file_stat_blocking {
    struct api_prim_blocking bl;
    struct ibio_gsstring_eval fn;
};

enum api_prim_execution_state
ibio_handle_prim_file_stat(struct api_thread *thread, struct gseprim *stat, struct api_prim_blocking **pblocking, gsvalue *pv)
{
    struct api_prim_blocking *blocking;
    struct ibio_file_stat_blocking *file_stat_blocking;

    if (blocking = *pblocking) {
        file_stat_blocking = (struct ibio_file_stat_blocking *)blocking;
    } else {
        blocking = *pblocking = ibio_file_stat_blocking_alloc();
        file_stat_blocking = (struct ibio_file_stat_blocking *)blocking;
        ibio_gsstring_eval_start(&file_stat_blocking->fn, stat->p.arguments[0]);
    }
    for (;;) {
        if (file_stat_blocking->fn.gss) {
            enum ibio_gsstring_eval_state st;

            st = ibio_gsstring_eval_advance(thread, stat->pos, &file_stat_blocking->fn);
            switch (st) {
                case ibio_gsstring_eval_blocked:
                    return api_st_blocked;
                case ibio_gsstring_eval_success:
                    break;
                default:
                    api_abend(thread, UNIMPL("%P: ibio_handle_prim_file_stat: ibio_gsstring_eval_advance st %d"), stat->pos, st);
                    return api_st_error;
            }
        } else {
            struct gsbio_dir *dir;
            struct gsstringbuilder *err;

            dir = gsbio_stat(file_stat_blocking->fn.sb->start);
            if (!dir) {
                err = gsreserve_string_builder();
                gsstring_builder_print(err, "%r");
                gsfinish_string_builder(err);
                api_abend(thread, "%P: %s", stat->pos, err->start);
                return api_st_error;
            }

            *pv = ibio_parse_gsbio_dir(stat->pos, dir);
            return api_st_success;
        }
    }
}

static api_prim_blocking_gccopy ibio_file_stat_blocking_gccopy;
static api_prim_blocking_gcevacuate ibio_file_stat_blocking_gcevacuate;
static api_prim_blocking_gccleanup ibio_file_stat_blocking_gccleanup;

struct api_prim_blocking *
ibio_file_stat_blocking_alloc()
{
    struct api_prim_blocking *res;
    struct ibio_file_stat_blocking *stat;

    res = api_blocking_alloc(
        sizeof(struct ibio_file_stat_blocking),
        ibio_file_stat_blocking_gccopy,
        ibio_file_stat_blocking_gcevacuate,
        ibio_file_stat_blocking_gccleanup
    );
    stat = (struct ibio_file_stat_blocking *)res;
    memset(&stat->fn, 0, sizeof(stat->fn));

    return res;
}

static
struct api_prim_blocking *
ibio_file_stat_blocking_gccopy(struct gsstringbuilder *err, struct api_prim_blocking *blocking)
{
    struct api_prim_blocking *newblocking;
    struct ibio_file_stat_blocking *stat, *newstat;

    newblocking = ibio_file_stat_blocking_alloc();

    stat = (struct ibio_file_stat_blocking *)blocking;
    newstat = (struct ibio_file_stat_blocking *)newblocking;

    memcpy((uchar*)newstat + sizeof(struct api_prim_blocking), (uchar*)stat + sizeof(struct api_prim_blocking), sizeof(struct ibio_file_stat_blocking) - sizeof(struct api_prim_blocking));

    return newblocking;
}

static
int
ibio_file_stat_blocking_gcevacuate(struct gsstringbuilder *err, struct api_prim_blocking *pblocking)
{
    gsstring_builder_print(err, UNIMPL("ibio_file_stat_blocking_gcevacuate"));
    return -1;
}

void
ibio_file_stat_blocking_gccleanup(struct api_prim_blocking *blocking)
{
}

gsvalue
ibio_parse_gsbio_dir(struct gspos pos, struct gsbio_dir *dir)
{
    gsvalue fields[ibio_stat_num_fields];

    fields[ibio_stat_mode_directory] = dir->d.mode & DMDIR ? gstrue(pos) : gsfalse(pos);
    fields[ibio_stat_name] = gscstringtogsstring(pos, dir->d.name);

    return gsrecordv(pos, ibio_stat_num_fields, fields);
}
