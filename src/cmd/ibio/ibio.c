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
    ibio_numprims,
};

static struct api_prim_table ibio_prim_table = {
    /* numprims = */ ibio_numprims,
};

void
gsadd_client_prim_sets()
{
}

enum {
    ibio_numrpcs = api_std_rpc_numrpcs,
};

static struct api_process_rpc_table ibio_rpc_table = {
    /* name = */ "IOLL Unix pool process",
    /* numrpcs = */ ibio_numrpcs,
    /* rpcs = */ {
        /* api_std_rpc_done */ api_main_process_handle_rpc_done,
        /* api_std_rpc_abend */ api_main_process_handle_rpc_abend,
    },
};

void
gsrun(char *script, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *ty)
{
    struct gstype *monad, *input, *output, *result, *primres;
    struct gstype *tyow, *tyoa;
    struct gstype *tyw;
    gsvalue stdout;
    char err[0x100];

    /* §section Cast down from newtype wrapper */

    tyw = ty;
    if (
        gstype_expect_app(err, err + sizeof(err), tyw, &tyw, &result) < 0
        || gstype_expect_app(err, err + sizeof(err), tyw, &tyw, &output) < 0
        || gstype_expect_app(err, err + sizeof(err), tyw, &tyw, &input) < 0
        || !(monad = tyw)
        || gstype_expect_abstract(err, err + sizeof(err), monad, "ibio.m") < 0
    ) {
        ace_down();
        gsfatal("%s: Bad type: %s", script, err);
    }

    if (!(prog = gscoerce(prog, ty, &tyw, err, err + sizeof(err), symtable, "ibio.out", input, output, result, 0))) {
        ace_down();
        gsfatal("%s: Couldn't cast down to primitive level: %s", script, err);
    }

    /* §section Pass in output */

    if (
        gstype_expect_lift(tyw, &tyw, err, err + sizeof(err)) < 0
        || gstype_expect_fun(tyw, &tyow, &tyw, err, err + sizeof(err)) < 0
        || gstype_expect_app(tyow, &tyow, &tyoa, err, err + sizeof(err)) < 0
        || gstype_expect_prim(tyow, gsprim_type_elim, "ibio.prim", "oport", err, err + sizeof(err)) < 0
        || gstypes_type_check(pos, tyoa, output, err, err + sizeof(err)) < 0
    ) {
        ace_down();
        gsfatal("%s: Not a function of output channel? (%s)", script, err);
    }

    stdout = ibio_oport_fdopen(1, err, err + sizeof(err));
    if (!stdout) {
        ace_down();
        gsfatal("%s: Couldn't open stdout: %s", script, err);
    }
    prog = gsapply(pos, prog, stdout);

    /* §section Paranoid check that the result is the API monad we epxect it to be */

    if (
        gstype_expect_lift(err, err + sizeof(err), tyw, &tyw) < 0
        || gstype_expect_app(err, err + sizeof(err), tyw, &tyw, &primres) < 0
        || gstypes_type_check(err, err + sizeof(err), pos, primres, result) < 0
        || gstype_expect_prim(err, err + sizeof(err), tyw, gsprim_type_api, "ibio.prim", "ibio") < 0
    ) {
        ace_down();
        gsfatal("%s: Bad type: %s", script, err);
    }

    /* §section Set up the IBIO thread */

    apisetupmainthread(&ibio_rpc_table, &ibio_prim_table, prog);
}
