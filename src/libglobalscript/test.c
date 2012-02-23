#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "test_tests.h"

void
p9main(int argc, char **argv)
{
    argv0 = *argv;
    test_macros();
    exits("");
}
