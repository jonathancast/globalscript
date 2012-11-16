#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

static struct gsregistered_primtype ibio_types[] = {
    /* name, file, line, group, kind, */
    { "ibio", __FILE__, __LINE__, gsprim_type_api, "u?^", },
    { "oport", __FILE__, __LINE__, gsprim_type_elim, "u*^", },
    { 0, },
};

enum {
    ibio_prim_unit,
    ibio_prim_write,
    ibio_prim_getargs,
    ibio_numprims,
};

static struct api_prim_table ibio_prim_table = {
    /* numprims = */ ibio_numprims,
    /* execs = */ {
        /* ibio_prim_unit = */ api_thread_handle_prim_unit,
        /* ibio_prim_write = */ ibio_handle_prim_write,
        /* ibio_prim_getargs = */ ibio_handle_prim_getargs,
    },
};

static struct gsregistered_prim ibio_operations[] = {
    /* name, file, line, group, check_type, index, */
    { "write", __FILE__, __LINE__, gsprim_operation_api, "ibio", "λ ο * ibio.prim.oport ο ` list.t ο ` ibio.prim.m 〈 〉 ⌊⌋ ` → → ∀", ibio_prim_write, },
    { "env.args.get", __FILE__, __LINE__, gsprim_operation_api, "ibio", "ibio.prim.m list.t list.t rune.t ` ` `", ibio_prim_getargs, },
    { 0, },
};

static struct gsregistered_primset ibio_primset = {
    /* name = */ "ibio.prim",
    /* types = */ ibio_types,
    /* operations = */ ibio_operations,
};

void
gsadd_client_prim_sets()
{
    gsprims_register_prim_set(&ibio_primset);
}

static struct api_thread_table ibio_thread_table = {
    /* thread_term_status = */ ibio_thread_term_status,
};

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
gsrun(char *script, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *ty, int argc, char **argv)
{
    struct gstype *monad, *input, *output, *result, *tybody;
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

    tybody = gstypes_compile_lift(pos, gstypes_compile_fun(pos,
        gstype_apply(pos,
            gstypes_compile_prim(pos, gsprim_type_elim, "ibio.prim", "oport", gskind_compile_string(pos, "u*^")),
            output
        ),
        gstypes_compile_lift(pos, gstype_apply(pos,
            gstypes_compile_prim(pos, gsprim_type_api, "ibio.prim", "ibio", gskind_compile_string(pos, "*?^")),
            result
        ))
    ));
    if (gstypes_type_check(err, err + sizeof(err), pos, tyw, tybody) < 0) {
        ace_down();
        gsfatal("%s: Panic!  Type after un-wrapping newtype wrapper incorrect (%s)", script, err);
    }

    /* §section Pass in output */

    if (ibio_write_threads_init(err, err + sizeof(err)) < 0) {
        ace_down();
        gsfatal("%s: Couldn't initialize write thread pool: %s", script, err);
    }

    stdout = ibio_oport_fdopen(1, err, err + sizeof(err));
    if (!stdout) {
        ace_down();
        gsfatal("%s: Couldn't open stdout: %s", script, err);
    }
    prog = gsapply(pos, prog, stdout);

    /* §section Set up the IBIO thread */

    apisetupmainthread(&ibio_rpc_table, &ibio_thread_table, ibio_main_thread_alloc_data(pos, argc, argv), &ibio_prim_table, prog);
}
