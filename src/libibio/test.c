#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "test_tests.h"
#include "test_systemtests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    start_tests();
    run_system_tests();
    test_iostat();
    test_iochan();
    exits("");
}
