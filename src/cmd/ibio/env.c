/* §source.file Environment Variables & Command-Line Arguments */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "ibio.h"

enum api_prim_execution_state
ibio_handle_prim_getargs(struct api_thread *thread, struct gsapiprim *apiprim, struct api_prim_blocking **pblocking, gsvalue *res)
{
    struct ibio_thread_data *data;

    data = api_thread_client_data(thread);
    *res = data->cmd_args;
    return api_st_success;
}
