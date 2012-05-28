#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    start_tests();
    test_macros();
    test_symtable();
    test_load();
    exits("");
}
