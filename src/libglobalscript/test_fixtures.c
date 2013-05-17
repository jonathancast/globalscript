#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_fixtures.h"

struct gspos
test_fixture_here(char *file, int lineno)
{
    struct gspos pos;

    pos.file = gsintern_string(gssymfilename, file);
    pos.lineno = lineno;
    pos.columnno = 0;

    return pos;
}
