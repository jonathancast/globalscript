#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsadd_client_prim_sets()
{
}

void
gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *symtable)
{
}

void
gscheck_program(char *doc, struct gsfile_symtable *symtable, struct gspos pos, struct gstype *ty)
{
}

int
gs_client_pre_ace_gc_trace_roots(struct gsstringbuilder *err)
{
    return 0;
}

void
gsrun(char *doc, struct gspos pos, gsvalue prog, int argc, char **argv)
{
    ace_down();
}
