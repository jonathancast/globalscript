#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

enum {
    exec_numrpcs = api_std_rpc_numrpcs,
};

static struct api_process_rpc_table exec_table = {
    /* name = */ "Exec Unix pool process",
    /* numrpcs = */ exec_numrpcs,
    /* rpcs = */ {
        /* api_std_rpc_done */ api_main_process_unimpl_rpc,
        /* api_std_rpc_abend */ api_main_process_handle_rpc_abend,
    },
};

void
gsrun(char *script, gsvalue prog, struct gstype *type)
{
    apisetupmainthread(&exec_table, prog);
}
