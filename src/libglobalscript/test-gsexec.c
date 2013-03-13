#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

static struct gsregistered_primtype test_sequence_types[] = {
    /* name, file, line, group, kind, */
    { "m", __FILE__, __LINE__, gsprim_type_api, "u?^", },
    { 0, },
};

enum test_sequence_api_op {
    test_sequence_api_op_unit,
    num_test_sequence_api_ops,
};

static struct gsregistered_prim test_sequence_operations[] = {
    /* name, file, line, group, type, index, */
    { "unit", __FILE__, __LINE__, gsprim_operation_api, "m", "λ α ? α \"apiprim test.sequence m α ` → ∀", test_sequence_api_op_unit, },
    { 0, },
};

static gsprim_handler *test_sequence_exec[] = {
};

static gsubprim_handler *test_sequence_ubexec[] = {
};

static gslprim_handler *test_sequence_lexec[] = {
};

static struct gsregistered_primset test_sequence_primset = {
    /* name = */ "test.sequence",
    /* types = */ test_sequence_types,
    /* operations = */ test_sequence_operations,
    /* exec_table = */ test_sequence_exec,
    /* ubexec_table = */ test_sequence_ubexec,
    /* lexec_table = */ test_sequence_lexec,
};

void
gsadd_client_prim_sets()
{
    gsprims_register_prim_set(&test_sequence_primset);
}

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

static enum api_prim_execution_state exec_thread_term_status(struct api_thread *);

static struct api_thread_table exec_thread_table = {
    /* thread_term_status = */ exec_thread_term_status,
};

static struct api_prim_table exec_prim_table = {
    /* numprims = */ num_test_sequence_api_ops,
    /* execs = */ {
        /* test_sequence_api_op_unit = */ api_thread_handle_prim_unit,
    },
};

void
gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *symtable)
{
}

void
gscheck_program(char *doc, struct gsfile_symtable *symtable, struct gspos pos, struct gstype *type)
{
}

int
gs_client_pre_ace_gc_trace_roots(struct gsstringbuilder *err)
{
    return 0;
}

void
gsrun(char *script, struct gspos pos, gsvalue prog, int argc, char **argv)
{
    apisetupmainthread(&exec_rpc_table, &exec_thread_table, 0, &exec_prim_table, prog);
}

static
enum api_prim_execution_state
exec_thread_term_status(struct api_thread *thread)
{
    return api_st_success;
}
