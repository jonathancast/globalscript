/* §source.file IBIO Threads (general stuff) */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

static gsvalue ibio_thread_data_trace(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info ibio_thread_data_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ ibio_thread_data_trace,
        /* description = */ "IBIO Thread Data",
    },
};

void *
ibio_main_thread_alloc_data(struct gspos entrypos, int argc, char **argv)
{
    struct ibio_thread_data *res;
    gsvalue gsargv[0x100];
    int i;

    res = gs_sys_global_block_suballoc(&ibio_thread_data_info, sizeof(struct ibio_thread_data));

    if (argc > sizeof(gsargv) / sizeof(*gsargv))
        gsfatal_unimpl(__FILE__, __LINE__, "Need to dynamically allocate gsargv")
    ;
    for (i = 0; i < argc; i++)
        gsargv[i] = gscstringtogsstring(entrypos, argv[i])
    ;
    res->cmd_args = gsarraytolist(entrypos, argc, gsargv);
    res->writing_to_oport = 0;
    res->reading_from_iport = 0;
    res->forward = 0;

    return res;
}

enum api_prim_execution_state
ibio_thread_term_status(struct api_thread *thread)
{
    struct ibio_thread_data *data;

    data = api_thread_client_data(thread);

    if (data->writing_to_oport)
        return api_st_blocked
    ;

    if (data->reading_from_iport)
        return api_st_blocked
    ;

    return api_st_success;
}

static
gsvalue
ibio_thread_data_trace(struct gsstringbuilder *err, gsvalue v)
{
    struct ibio_thread_data *data, *newdata;
    gsvalue gctemp;

    data = (struct ibio_thread_data *)v;

    if (data->forward) {
        gsstring_builder_print(err, UNIMPL("ibio_thread_data_trace: check for forward"));
        return 0;
    }

    newdata = gs_sys_global_block_suballoc(&ibio_thread_data_info, sizeof(struct ibio_thread_data));

    memcpy(newdata, data, sizeof(*newdata));

    data->forward = newdata;

    if (GS_GC_TRACE(err, &newdata->cmd_args) < 0) return 0;

    if (newdata->writing_to_oport) {
        gsstring_builder_print(err, UNIMPL("ibio_thread_data_trace: evacuate writing_to_oport"));
        return 0;
    }

    if (newdata->reading_from_iport) {
        gsstring_builder_print(err, UNIMPL("ibio_thread_data_trace: evacuate reading_from_iport"));
        return 0;
    }

    return (gsvalue)newdata;
}
