#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"

#include "ibio.h"
#include "stat.h"

static void TEST_FILE_STAT_BLOCKING_GCCOPY(void);

void
test_stat()
{
    RUNTESTS(TEST_FILE_STAT_BLOCKING_GCCOPY);
}

void
TEST_FILE_STAT_BLOCKING_GCCOPY()
{
    struct api_prim_blocking *stat, *newstat;
    struct gsstringbuilder *err;

    stat = ibio_file_stat_blocking_alloc();

    err = gsreserve_string_builder();

    gs_sys_wait_for_gc();
    if (gs_sys_start_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);

    err = gsreserve_string_builder();
    newstat = stat->gccopy(err, stat);
    gsfinish_string_builder(err);

    ok(__FILE__, __LINE__, !!newstat, "ibio_prim_iptr_deref_blocking_gccopy failed: %s", err->start);

    err = gsreserve_string_builder();
    if (gs_sys_finish_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);
}
