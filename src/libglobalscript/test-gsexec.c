#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

static struct gsregistered_primkind kyunl = {
    /* node = */ gsprim_kind_unlifted,
};

static struct gsregistered_primkind kyunk = {
    /* node = */ gsprim_kind_unknown,
};

static struct gsregistered_primkind kym = {
    /* node = */ gsprim_kind_exponent,
    /* base = */ &kyunl,
    /* exponent = */ &kyunk,
};

static struct gsregistered_primtype test_sequence_types[] = {
    /* name, file, line, group, kind, */
    { "m", __FILE__, __LINE__, gsprim_type_api, &kym, },
    { 0, },
};

static int check_unit_type(struct gstype *, char *, char *);

enum test_sequence_api_op {
    test_sequence_api_op_unit,
    num_test_sequence_api_ops,
};

static struct gsregistered_prim test_sequence_operations[] = {
    /* name, file, line, group, check_type, index, */
    { "unit", __FILE__, __LINE__, gsprim_operation_api, "m", check_unit_type, test_sequence_api_op_unit, },
    { 0, },
};

static struct gsregistered_primset test_sequence_primset = {
    /* name = */ "test.sequence",
    /* types = */ test_sequence_types,
    /* operations = */ test_sequence_operations,
};

void
gsadd_client_prim_sets()
{
    gsprims_register_prim_set(&test_sequence_primset);
}

static
int
check_unit_type(struct gstype *ty, char *err, char *eerr)
{
    struct gstype *tyw;
    gsinterned_string alpha;
    struct gstype *arg, *res;

    tyw = ty;
    if (
        gstype_expect_forall(tyw, &alpha, &tyw, err, eerr) < 0
        || gstype_expect_fun(tyw, &arg, &tyw, err, eerr) < 0
        || gstype_expect_var(arg, alpha, err, eerr) < 0
        || gstype_expect_app(tyw, &tyw, &res, err, eerr) < 0
        || gstype_expect_prim(tyw, gsprim_type_api, "test.sequence", "m", err, eerr) < 0
        || gstype_expect_var(res, alpha, err, eerr) < 0
    )
        return -1
    ;

    return 0;
}

static struct api_prim_table exec_prim_table = {
    /* numprims = */ num_test_sequence_api_ops,
    /* execs = */ {
        /* test_sequence_api_op_unit = */ api_thread_handle_prim_unit,
    },
};

enum {
    exec_numrpcs = api_std_rpc_numrpcs,
};

static struct api_process_rpc_table exec_rpc_table = {
    /* name = */ "Exec Unix pool process",
    /* numrpcs = */ exec_numrpcs,
    /* rpcs = */ {
        /* api_std_rpc_done */ api_main_process_handle_rpc_done,
        /* api_std_rpc_abend */ api_main_process_handle_rpc_abend,
    },
};

void
gsrun(char *script, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *type)
{
    apisetupmainthread(&exec_rpc_table, &exec_prim_table, prog);
}
