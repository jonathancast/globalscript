#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "ace.h"

#include "test_tests.h"

#include "test_fixtures.h"

static void TEST_ERASE_BOTTOM_OF_STACK(void);

void
test_acestack()
{
    RUNTESTS(TEST_ERASE_BOTTOM_OF_STACK);
}

static void test_fixture_update(struct ace_thread *, struct gsbc_cont_update **, gsvalue *);

void
TEST_ERASE_BOTTOM_OF_STACK()
{
    struct ace_thread *thread;
    struct gsbc_cont *top;
    struct gsbc_cont_update *bottom_update, *top_update;
    gsvalue bottom_gsv, top_gsv, gctemp;
    struct gsbc_cont_app *app;
    struct gsstringbuilder *err;
    int res;

    thread = ace_thread_alloc();

    test_fixture_update(thread, &bottom_update, &bottom_gsv);
    app = ace_push_app(HERE, thread, 2, gserror(HERE, "First argument"), gserror(HERE, "Second argument"));
    test_fixture_update(thread, &top_update, &top_gsv);

    err = gsreserve_string_builder();

    gs_sys_wait_for_gc();
    if (gs_sys_start_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }

    gsfinish_string_builder(err);

    err = gsreserve_string_builder();
    res = GS_GC_TRACE(err, &top_gsv);
    gsfinish_string_builder(err);

    ok(__FILE__, __LINE__, res >= 0, "GS_GC_TRACE failed: %s", err->start);

    ok_long_eq(__FILE__, __LINE__, thread->state, ace_thread_gcforward, "Thread wasn't forwarded");
    thread = thread->st.forward.dest;

    *(ulong*)thread->stackbot = 0xdeadbeef;

    err = gsreserve_string_builder();
    res = ace_stack_post_gc_consolidate(err, thread);
    gsfinish_string_builder(err);

    ok(__FILE__, __LINE__, res >= 0, "ace_stack_post_gc_consolidate failed: %s", err->start);
    ok_ulong_eq(__FILE__, __LINE__, *(ulong*)thread->stackbot, 0xdeadbeef, "ace_stack_post_gc_consolidate overran the bottom of the stack");

    err = gsreserve_string_builder();
    if (gs_sys_finish_gc(err) < 0) {
        gsfinish_string_builder(err);
        ok(__FILE__, __LINE__, 0, "GC failed: %s", err->start);
    }
    gsfinish_string_builder(err);

    top = ace_stack_top(thread);
    ok_long_eq(__FILE__, __LINE__, top->node, gsbc_cont_update, "Don't have an update frame on top of the stack");
    top_update = (struct gsbc_cont_update *)top;
    ok_ulong_eq(__FILE__, __LINE__, (gsvalue)top_update->dest, top_gsv, "Update frame isn't updated to point at the right location");

    ace_pop_update(thread);
    not_ok(__FILE__, __LINE__, !!ace_stack_top(thread), "Didn't discard the frames below the lowest live update frame");
}

void
test_fixture_update(struct ace_thread *thread, struct gsbc_cont_update **pupdate, gsvalue *php)
{
    struct gsheap_item *hp;

    hp = gsreserveheap(sizeof(struct gsindirection));
    memset(&hp->lock, 0, sizeof(hp->lock));
    hp->pos = HERE;
    hp->type = gseval;

    *pupdate = ace_push_update(HERE, thread, hp);
    gsblackhole_heap(hp, *pupdate);
    *php = (gsvalue)hp;
}
