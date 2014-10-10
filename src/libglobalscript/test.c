#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"
#include "test_systemtests.h"

static int gsPfmt(Fmt *f);
static int gsyfmt(Fmt *f);

void
p9main(int argc, char **argv)
{
    argv0 = *argv;

    fmtinstall('P', gsPfmt);
    fmtinstall('y', gsyfmt);

    start_tests();
    test_macros();
    test_symtable();
    test_load();
    test_typealloc();
    test_clienttypecheck();
    test_eval();
    test_acestack();
    run_system_tests();
    test_iostat();
    test_iochan();
    exits("");
}

int
gsPfmt(Fmt *f)
{
    struct gspos pos;

    pos = va_arg(f->args, struct gspos);
    return pos.columnno > 0
        ? fmtprint(f, "%s:%d:%d", pos.file->name, pos.lineno, pos.columnno)
        : pos.lineno > 0
        ? fmtprint(f, "%s:%d", pos.file->name, pos.lineno)
        : fmtprint(f, "%s", pos.file->name)
    ;
}

int
gsyfmt(Fmt *f)
{
    gsinterned_string sym;

    sym = va_arg(f->args, gsinterned_string);
    return fmtprint(f, "%s", sym->name);
}
