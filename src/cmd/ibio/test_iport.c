#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"

#include "ibio.h"
#include "iport.h"

static void TEST_IPTR_DEREF_GCCOPY(void);

void
test_iport()
{
    RUNTESTS(TEST_IPTR_DEREF_GCCOPY);
}

void
TEST_IPTR_DEREF_GCCOPY()
{
    struct gslprim_blocking *blocking, *newblocking;
    struct gsstringbuilder *err;

    blocking = ibio_prim_iptr_deref_blocking_alloc();

    err = gsreserve_string_builder();

    gs_sys_wait_for_gc();
    if (gs_sys_start_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);

    err = gsreserve_string_builder();
    newblocking = blocking->gccopy(err, blocking);
    gsfinish_string_builder(err);

    ok(__FILE__, __LINE__, !!newblocking, "ibio_prim_iptr_deref_blocking_gccopy failed: %s", err->start);

    err = gsreserve_string_builder();
    if (gs_sys_finish_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);
}
