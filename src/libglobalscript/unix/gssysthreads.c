#define NOPLAN9DEFINES 1
#include <u.h>
#include <libc.h>

#ifdef  __linux__
#include <sys/mman.h>
#include <sched.h>
#endif

#include <signal.h>

#include <libglobalscript.h>

#include "../gsproc.h"

struct gsthread_pool_descr {
    void (*fn)(void *);
    void *arg;
};

static void *gs_sys_unix_stack_nursury;
static int gs_sys_unix_page_size;

static int gsthread_pool_main(void *);

int
gscreate_thread_pool(void (*fn)(void *), void *arg, ulong sz)
{
#   ifdef  __linux__
    void *stack, *top_of_stack;
    struct gsthread_pool_descr *pool_descr;
    int pid;

    if (!gs_sys_unix_stack_nursury) gs_sys_unix_stack_nursury = (void*)GS_MAX_PTR;

    stack = gs_sys_unix_stack_nursury;
    if (mmap(stack, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == (void *)-1)
        return -1
    ;

    if (!gs_sys_unix_page_size)
        if ((gs_sys_unix_page_size = sysconf(_SC_PAGE_SIZE)) < 0)
            return -1
    ;

    if (mprotect(stack, gs_sys_unix_page_size, PROT_NONE) < 0) return -1;

    gs_sys_unix_stack_nursury = top_of_stack = (uchar*)stack + BLOCK_SIZE;

    top_of_stack = (uchar*)top_of_stack - sz;
    top_of_stack = (uchar*)top_of_stack - (uintptr)top_of_stack % sizeof(void*);

    memcpy(top_of_stack, arg, sz);
    arg = top_of_stack;

    top_of_stack = (uchar*)top_of_stack - sizeof(*pool_descr);
    pool_descr = top_of_stack;

    pool_descr->fn = fn;
    pool_descr->arg = arg;

    lock(&gs_allocator_lock);
    gs_sys_num_procs++;
    unlock(&gs_allocator_lock);

    if ((pid = clone(gsthread_pool_main, top_of_stack, CLONE_VM | SIGCHLD, pool_descr)) < 0) {
        lock(&gs_allocator_lock);
        gs_sys_num_procs--;
        unlock(&gs_allocator_lock);
    }
    return pid;
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

    lock(&gs_allocator_lock);
    gs_sys_num_procs--;
    unlock(&gs_allocator_lock);

    return 0;
}
