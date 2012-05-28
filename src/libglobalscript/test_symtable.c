#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "gsinputfile.h"

#include "test_tests.h"

static void TEST_INTERN_IDEMPOTENT(void);

void
test_symtable(void)
{
    RUNTESTS(TEST_INTERN_IDEMPOTENT);
}

static
void
TEST_INTERN_IDEMPOTENT(void)
{
    gsinterned_string s0, s1, s2;

    s0 = gsintern_string(gssymfilename, "./a.ags");
    s1 = gsintern_string(gssymfilename, "./a.ags");
    s2 = gsintern_string(gssymfilename, "./b.ags");

    ok_ulong_eq(__FILE__, __LINE__, (uintptr)s0, (uintptr)s1, "gsintern_string not idempotent");
    ok_ulong_ne(__FILE__, __LINE__, (uintptr)s0, (uintptr)s2, "gsintern_string returned the same value for two different strings");
}

