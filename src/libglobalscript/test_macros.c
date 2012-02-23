#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "test_tests.h"

static void TEST_BLOCK_CONTAINING(void);
static void TEST_END_OF_BLOCK(void);

void
test_macros()
{
    TEST_BLOCK_CONTAINING();
    TEST_END_OF_BLOCK();
}

static
void
TEST_BLOCK_CONTAINING(void)
{
    fprint(2, "%s\n", "----\t" __FILE__ "\t TEST_BLOCK_CONTAINING\t----");

    gsassert_ulong_eq(__FILE__, __LINE__, (uintptr)BLOCK_CONTAINING(0x9300004), (uintptr)0x9300000, "BLOCK_CONTAINING incorrect");
    gsassert_ulong_eq(__FILE__, __LINE__, (uintptr)BLOCK_CONTAINING(0x9300000), (uintptr)0x9300000, "BLOCK_CONTAINING incorrect");
}

static
void
TEST_END_OF_BLOCK()
{
    fprint(2, "%s\n", "----\t" __FILE__ "\t TEST_END_OF_BLOCK\t----");

    gsassert_ulong_eq(__FILE__, __LINE__, (uintptr)END_OF_BLOCK(0x9300000), (uintptr)0x9400000, "END_OF_BLOCK incorrect");
}

