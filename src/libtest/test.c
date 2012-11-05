#include <u.h>
#include <libc.h>
#include <libtest.h>

#include "test_tests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    start_tests();
    test_predicates();
    exits("");
}
