#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"

static void TEST_BLOCK_CONTAINING(void);
static void TEST_END_OF_BLOCK(void);

void
test_macros()
{
    RUNTESTS(TEST_BLOCK_CONTAINING);
    RUNTESTS(TEST_END_OF_BLOCK);
}

static
void
TEST_BLOCK_CONTAINING(void)
{
    ok_uintptr_eq(__FILE__, __LINE__, (uintptr)BLOCK_CONTAINING(0x30C0001 * sizeof(gsvalue)), (uintptr)(0x30C0000 * sizeof(gsvalue)), "BLOCK_CONTAINING incorrect");
    ok_uintptr_eq(__FILE__, __LINE__, (uintptr)BLOCK_CONTAINING(0x30C0000 * sizeof(gsvalue)), (uintptr)(0x30C0000 * sizeof(gsvalue)), "BLOCK_CONTAINING incorrect");
}

static
void
TEST_END_OF_BLOCK()
{
    ok_uintptr_eq(__FILE__, __LINE__, (uintptr)END_OF_BLOCK(0x30C0000 * sizeof(gsvalue)), (uintptr)(0x3100000 * sizeof(gsvalue)), "END_OF_BLOCK incorrect");
}
