#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "test_tests.h"
#include "test_systemtests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    run_system_tests();
    test_iostat();
    exits("");
}
