#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

enum {
    ibio_numrpcs = api_std_rpc_numrpcs,
};

static struct api_process_rpc_table exec_table = {
    /* name = */ "IOLL Unix pool process",
    /* numrpcs = */ ibio_numrpcs,
    /* rpcs = */ {
        /* api_std_rpc_done */ api_main_process_unimpl_rpc,
        /* api_std_rpc_abend */ api_main_process_handle_rpc_abend,
    },
};

void
gsrun(char *script, struct gsfile_symtable *symtable, gsvalue prog, struct gstype *ty)
{
    struct gstype *monad, *input, *output, *result;
    struct gstype *tyw;
    char err[0x100];

    tyw = ty;
    if (
        gstype_expect_app(tyw, &tyw, &result, err, err + sizeof(err)) < 0
        || gstype_expect_app(tyw, &tyw, &output, err, err + sizeof(err)) < 0
        || gstype_expect_app(tyw, &tyw, &input, err, err + sizeof(err)) < 0
        || !(monad = tyw)
        || gstype_expect_abstract(monad, "ibio.m", err, err + sizeof(err)) < 0
    ) {
        ace_down();
        gsfatal("%s: Bad type: %s", script, err);
    }

    if (!(prog = gscoerce(prog, ty, &tyw, err, err + sizeof(err), symtable, "ibio.out", input, output, result, 0))) {
        ace_down();
        gsfatal("%s: Couldn't cast down to primitive level: %s", script, err);
    }

    apisetupmainthread(&exec_table, prog);
}
