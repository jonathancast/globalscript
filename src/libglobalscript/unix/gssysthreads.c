#define NOPLAN9DEFINES 1
#include <u.h>
#include <libc.h>

#ifdef  __linux__
#include <sched.h>
#endif

#include <signal.h>

#include <libglobalscript.h>

#include "../gsproc.h"

static struct gs_block_class gssys_stack_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
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
    void *stack, *top_of_stack;
    struct gsthread_pool_descr *pool_descr;

    stack = gs_sys_block_alloc(&gssys_stack_descr);

    top_of_stack = (uchar*)stack + BLOCK_SIZE;

    top_of_stack = (uchar*)top_of_stack - sz;
    top_of_stack = (uchar*)top_of_stack - (uintptr)top_of_stack % sizeof(void*);

    memcpy(top_of_stack, arg, sz);
    arg = top_of_stack;

    top_of_stack = (uchar*)top_of_stack - sizeof(*pool_descr);
    pool_descr = top_of_stack;

    pool_descr->fn = fn;
    pool_descr->arg = arg;

    return clone(
        gsthread_pool_main,
        top_of_stack,
        CLONE_VM | SIGCHLD,
        pool_descr
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

    lock(&gs_allocator_lock);
    gs_sys_num_procs++;
    unlock(&gs_allocator_lock);

    pdescr = (struct gsthread_pool_descr *)arg;
    pdescr->fn(pdescr->arg);

    lock(&gs_allocator_lock);
    gs_sys_num_procs--;
    unlock(&gs_allocator_lock);

    return 0;
}
