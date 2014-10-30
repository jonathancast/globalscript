#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"
#include "test_systemtests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    start_tests();
    test_iport();
    run_system_tests();
    exits("");
}
