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
    { "iport", __FILE__, __LINE__, gsprim_type_elim, "u*^", },
    { "oport", __FILE__, __LINE__, gsprim_type_elim, "u*^", },
    { "iptr", __FILE__, __LINE__, gsprim_type_elim, "u*^", },
    { 0, },
};

enum {
    ibio_prim_unit,
    ibio_prim_write,
    ibio_prim_read,
    ibio_prim_getargs,
    ibio_prim_file_stat,
    ibio_numprims,
};

static struct api_prim_table ibio_prim_table = {
    /* numprims = */ ibio_numprims,
    /* execs = */ {
        /* ibio_prim_unit = */ api_thread_handle_prim_unit,
        /* ibio_prim_write = */ ibio_handle_prim_write,
        /* ibio_prim_read = */ ibio_handle_prim_read,
        /* ibio_prim_getargs = */ ibio_handle_prim_getargs,
        /* ibio_prim_file_stat = */ ibio_handle_prim_file_stat,
    },
};

static gsprim_handler *ibio_exec[] = {
};

enum {
    ibio_prim_iptr_iseof,
};

static gsubprim_handler *ibio_ubexec[] = {
    ibio_prim_iptr_handle_iseof,
};

static struct gsregistered_prim ibio_operations[] = {
    /* name, file, line, group, check_type, index, */
    { "unit", __FILE__, __LINE__, gsprim_operation_api, "ibio", "λ α ? α ibio.prim.m α ` → ∀", ibio_prim_unit, },
    { "write", __FILE__, __LINE__, gsprim_operation_api, "ibio", "λ ο * ibio.prim.oport ο ` list.t ο ` ibio.prim.m \"Π〈 〉 ⌊⌋ ` → → ∀", ibio_prim_write, },
    { "read", __FILE__, __LINE__, gsprim_operation_api, "ibio", "λ ι * ibio.prim.iport ι ` ibio.acceptor.prim.t ι ` \"Π〈 〉 ⌊⌋ ` ibio.prim.m ibio.prim.iptr ι ` ⌊⌋ ` → → ∀", ibio_prim_read, },
    { "env.args.get", __FILE__, __LINE__, gsprim_operation_api, "ibio", "ibio.prim.m list.t list.t rune.t ` ` `", ibio_prim_getargs, },
    { "file.stat", __FILE__, __LINE__, gsprim_operation_api, "ibio", "list.t rune.t ` ibio.prim.m " IBIO_DIR_TYPE " ` →", ibio_prim_file_stat, },
    { "iptr.iseof", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "λ s * ibio.prim.iptr s ` \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → ∀", ibio_prim_iptr_iseof, },
    { 0, },
};

static struct gsregistered_primset ibio_primset = {
    /* name = */ "ibio.prim",
    /* types = */ ibio_types,
    /* operations = */ ibio_operations,
    /* exec_table */ ibio_exec,
    /* ubexec_table */ ibio_ubexec,
};

void
gsadd_client_prim_sets()
{
    gsprims_register_prim_set(&ibio_primset);
}

static struct api_thread_table ibio_thread_table = {
    /* thread_term_status = */ ibio_thread_term_status,
};

static struct api_process_rpc_table ibio_rpc_table = {
    /* name = */ "IOLL Unix pool process",
    /* numrpcs = */ ibio_numrpcs,
    /* rpcs = */ {
        /* api_std_rpc_done */ api_main_process_handle_rpc_done,
        /* api_std_rpc_abend */ api_main_process_handle_rpc_abend,
        /* ibio_uxproc_rpc_stat */ ibio_main_process_handle_rpc_stat,
    },
};

static gsvalue ibio_downcast_ibio_m(char *, char *, char *, struct gsfile_symtable *, struct gspos, gsvalue, struct gstype *, struct gstype **, struct gstype **, struct gstype **);

void
gsrun(char *script, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *ty, int argc, char **argv)
{
    struct gstype *input, *output, *result;
    gsvalue stdin, stdout;
    struct gsstringbuilder err;
    char errbuf[0x100];

    /* §section Check various types look like we expect */
    ibio_check_acceptor_type(script, symtable, pos);

    /* §section Cast down from newtype wrapper */

    prog = ibio_downcast_ibio_m(errbuf, errbuf + sizeof(errbuf), script, symtable, pos, prog, ty, &input, &output, &result);

    /* §section Pass in input */

    err = gsreserve_string_builder();
    if (gstypes_type_check(&err, pos, input, gstypes_compile_rune(pos)) < 0) {
        gsfinish_string_builder(&err);
        ace_down();
        gsfatal("%s: Panic!  Non-rune.t input in main program (%s)", script, err.start);
    }
    gsfinish_string_builder(&err);

    if (ibio_read_threads_init(errbuf, errbuf + sizeof(errbuf)) < 0) {
        ace_down();
        gsfatal("%s: Couldn't initialize read thread pool: %s", script, errbuf);
    }
    stdin = ibio_iport_fdopen(0, errbuf, errbuf + sizeof(errbuf));
    if (!stdin) {
        ace_down();
        gsfatal("%s: Couldn't open stdin: %s", script, errbuf);
    }
    prog = gsapply(pos, prog, stdin);

    /* §section Pass in output */

    err = gsreserve_string_builder();
    if (gstypes_type_check(&err, pos, output, gstypes_compile_rune(pos)) < 0) {
        gsfinish_string_builder(&err);
        ace_down();
        gsfatal("%s: Panic!  Non-rune.t output in main program (%s)", script, err.start);
    }
    gsfinish_string_builder(&err);

    if (ibio_write_threads_init(errbuf, errbuf + sizeof(errbuf)) < 0) {
        ace_down();
        gsfatal("%s: Couldn't initialize write thread pool: %s", script, err);
    }

    stdout = ibio_oport_fdopen(1, errbuf, errbuf + sizeof(errbuf));
    if (!stdout) {
        ace_down();
        gsfatal("%s: Couldn't open stdout: %s", script, errbuf);
    }
    prog = gsapply(pos, prog, stdout);

    /* §section Set up the IBIO thread */

    apisetupmainthread(&ibio_rpc_table, &ibio_thread_table, ibio_main_thread_alloc_data(pos, argc, argv), &ibio_prim_table, prog);
}

static
gsvalue
ibio_downcast_ibio_m(char *errbuf, char *eerrbuf, char *script, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *ty, struct gstype **pinput, struct gstype **poutput, struct gstype **presult)
{
    struct gstype *monad, *tybody;
    struct gstype *tyw;
    struct gsstringbuilder err;

    tyw = ty;
    if (
        gstype_expect_app(errbuf, eerrbuf, tyw, &tyw, presult) < 0
        || gstype_expect_app(errbuf, eerrbuf, tyw, &tyw, poutput) < 0
        || gstype_expect_app(errbuf, eerrbuf, tyw, &tyw, pinput) < 0
        || !(monad = tyw)
        || gstype_expect_abstract(errbuf, eerrbuf, monad, "ibio.m") < 0
    ) {
        ace_down();
        gsfatal("%s: Bad type: %s", script, errbuf);
    }

    err = gsreserve_string_builder();
    if (!(prog = gscoerce(prog, ty, &tyw, &err, symtable, "ibio.out", *pinput, *poutput, *presult, 0))) {
        gsfinish_string_builder(&err);
        ace_down();
        gsfatal("%s: Couldn't cast down to primitive level: %s", script, err);
    }
    gsfinish_string_builder(&err);

    /* §section Paranoid check that the result is the API monad we expect it to be */

    tybody = gstypes_compile_lift(pos, gstypes_compile_fun(pos,
        gstype_apply(pos,
            gstypes_compile_prim(pos, gsprim_type_elim, "ibio.prim", "iport", gskind_compile_string(pos, "u*^")),
            *pinput
        ),
        gstypes_compile_lift(pos, gstypes_compile_fun(pos,
            gstype_apply(pos,
                gstypes_compile_prim(pos, gsprim_type_elim, "ibio.prim", "oport", gskind_compile_string(pos, "u*^")),
                *poutput
            ),
            gstypes_compile_lift(pos, gstype_apply(pos,
                gstypes_compile_prim(pos, gsprim_type_api, "ibio.prim", "ibio", gskind_compile_string(pos, "*?^")),
                *presult
            ))
        ))
    ));
    err = gsreserve_string_builder();
    if (gstypes_type_check(&err, pos, tyw, tybody) < 0) {
        gsfinish_string_builder(&err);
        ace_down();
        gsfatal("%s: Panic!  Type after un-wrapping newtype wrapper incorrect (%s)", script, err);
    }
    gsfinish_string_builder(&err);

    return prog;
}
