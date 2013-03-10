/* §source.file IBIO Threads (general stuff) */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

static struct gs_sys_global_block_suballoc_info ibio_thread_data_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
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
