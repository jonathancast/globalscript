#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "test_tests.h"

static void TEST_GC_IMPLEMENTATION_FAILURE(void);

void
test_eval()
{
    RUNTESTS(TEST_GC_IMPLEMENTATION_FAILURE);
}

#define ERROR_MESSAGE "Something went wrong"
#define GS_SRC_FILE "./foo.ags"
#define GS_SRC_LINENO 42

void
TEST_GC_IMPLEMENTATION_FAILURE()
{
    int lineno;
    struct gspos srcpos;
    struct gsimplementation_failure *impl_failure, *old_impl_failure, *new_failure;
    gsvalue gcimpl_failure, gctemp;
    struct gsstringbuilder *err;
    int res;

    lineno = __LINE__;
    memset(&srcpos, 0, sizeof(srcpos));
    srcpos.file = gsintern_string(gssymfilename, GS_SRC_FILE);
    srcpos.lineno = GS_SRC_LINENO;
    impl_failure = gsunimpl(__FILE__, lineno, srcpos, ERROR_MESSAGE);

    err = gsreserve_string_builder();
    gs_sys_wait_for_gc();
    if (gs_sys_start_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }

    gsfinish_string_builder(err);

    old_impl_failure = impl_failure;
    gcimpl_failure = (gsvalue)impl_failure;
    err = gsreserve_string_builder();
    res = GS_GC_TRACE(err, &gcimpl_failure);
    gsfinish_string_builder(err);
    impl_failure = (struct gsimplementation_failure *)gcimpl_failure;

    ok(__FILE__, __LINE__, res >= 0, "GS_GC_TRACE failed: %s", err->start);

    err = gsreserve_string_builder();
    if (gs_sys_finish_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);

    ok_ptr_ne(__FILE__, __LINE__, impl_failure, old_impl_failure, "impl_failure wasn't updated to the right location");

    ok_ptr_ne(__FILE__, __LINE__, impl_failure->cpos.file, old_impl_failure->cpos.file, "impl_failure->cpos wasn't updated to the right location");
    ok_cstring_eq(__FILE__, __LINE__, impl_failure->cpos.file->name, __FILE__, "C Position (file name)");
    ok_ulong_eq(__FILE__, __LINE__, impl_failure->cpos.lineno, lineno, "C Position (file name)");

    ok_ptr_ne(__FILE__, __LINE__, impl_failure->srcpos.file, old_impl_failure->srcpos.file, "impl_failure wasn't updated to the right location");
    ok_cstring_eq(__FILE__, __LINE__, impl_failure->srcpos.file->name, GS_SRC_FILE, "GS Position (file name)");
    ok_ulong_eq(__FILE__, __LINE__, impl_failure->srcpos.lineno, GS_SRC_LINENO, "GS Position (file name)");

    ok_cstring_eq(__FILE__, __LINE__, impl_failure->message, ERROR_MESSAGE, "Message");

    new_failure = gsunimpl(__FILE__, __LINE__, impl_failure->srcpos, ERROR_MESSAGE);

    ok_ptr_ge(__FILE__, __LINE__, new_failure, impl_failure->message + strlen(impl_failure->message) + 1, "Fresh allocation overlaps with the GCed error");
}
