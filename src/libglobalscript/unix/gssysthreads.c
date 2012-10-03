#define NOPLAN9DEFINES 1
#include <u.h>
#include <libc.h>

#ifdef  __linux__
#include <sched.h>
#endif

#include <signal.h>

#include <libglobalscript.h>

static struct gs_block_class gssys_stack_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "C call stack",
};

struct gsthread_pool_descr {
    void (*fn)(void *);
    void *arg;
};

static int gsthread_pool_main(void *);

int
gscreate_thread_pool(void (*fn)(void *), void *arg, ulong sz)
{
#   ifdef  __linux__
    void *stack;
    struct gsthread_pool_descr pool_descr;

    pool_descr.fn = fn;
    pool_descr.arg = arg;

    stack = gs_sys_seg_alloc(&gssys_stack_descr);

    gswarning("%s:%d: parent stack near 0x%p", __FILE__, __LINE__, &stack);
    gswarning("%s:%d: (have to take address of stack or will get really weird early termination bug)", __FILE__, __LINE__);
    return clone(
        gsthread_pool_main,
        (uchar*)stack + BLOCK_SIZE,
        CLONE_VM | SIGCHLD,
        &pool_descr
    );
#   else
    werrstr("%s:%d: unimpl gscreate_thread_pool", __FILE__, __LINE__);
    return -1;
#   endif
}

static
int
gsthread_pool_main(void *arg)
{
    struct gsthread_pool_descr *pdescr;

    pdescr = (struct gsthread_pool_descr *)arg;
    pdescr->fn(pdescr->arg);
    return 0;
}
