#include <u.h>
#include <libc.h>
#include <libtest.h>

#include "test_tests.h"

static void TEST_SIGNED_COMPARISONS(void);

void
test_predicates()
{
    RUNTESTS(TEST_SIGNED_COMPARISONS);
}

static
void
TEST_SIGNED_COMPARISONS()
{
    ok_long_lt(__FILE__, __LINE__, -1, 0, "-1 is somehow >= 0");
    ok_long_eq(__FILE__, __LINE__, 0, 0, "0 is somehow != 0");
}
