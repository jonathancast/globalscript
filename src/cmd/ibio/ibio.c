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
    struct gstype *monad, *input, *output, *result, *tybody;
    struct gstype *tyow;
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

    /* §section Paranoid check that the result is the API monad we expect it to be */

    tybody = gstypes_compile_lift(pos, gstype_apply(pos,
        gstypes_compile_prim(pos, gsprim_type_api, "ibio.prim", "ibio", gskind_compile_string(pos, "*?^")),
        result
    ));
    if (
        gstypes_type_check(err, err + sizeof(err), pos, tyw,
            gstypes_compile_lift(pos, gstypes_compile_fun(pos,
                gstype_apply(pos,
                    gstypes_compile_prim(pos, gsprim_type_elim, "ibio.prim", "oport", gskind_compile_string(pos, "u*^")),
                    output
                ),
                tybody
            ))
        ) < 0
    ) {
        ace_down();
        gsfatal("%s: Panic!  Type after un-wrapping newtype wrapper incorrect (%s)", script, err);
    }

    /* §section Pass in output */

    stdout = ibio_oport_fdopen(1, err, err + sizeof(err));
    if (!stdout) {
        ace_down();
        gsfatal("%s: Couldn't open stdout: %s", script, err);
    }
    prog = gsapply(pos, prog, stdout);

    /* §section Set up the IBIO thread */

    apisetupmainthread(&ibio_rpc_table, &ibio_prim_table, prog);
}
