#define NOPLAN9DEFINES 1

#ifdef  __linux__
#define _GNU_SOURCE 1
#endif

#include <u.h>
#include <libc.h>

#ifdef  __linux__
#include <sys/mman.h>
#include <sched.h>
#include <sys/prctl.h>
#endif

#include <signal.h>

#include <stdatomic.h>
#include <libglobalscript.h>

#include "../gsproc.h"

struct gsthread_pool_descr {
    void (*fn)(void *);
    void *arg;
};

static int gs_sys_unix_page_size;

static int gsthread_pool_main(void *);

int
gscreate_thread_pool(void (*fn)(void *), void *arg, ulong sz)
{
#   ifdef  __linux__
    void *stack, *top_of_stack;
    struct gsthread_pool_descr *pool_descr;
    int pid;

    stack = mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (stack == (void *)-1) return -1;

    if (!gs_sys_unix_page_size)
        if ((gs_sys_unix_page_size = sysconf(_SC_PAGE_SIZE)) < 0)
            return -1
    ;

    if (mprotect(stack, gs_sys_unix_page_size, PROT_NONE) < 0) return -1;

    top_of_stack = (uchar*)stack + BLOCK_SIZE;

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

    if ((pid = clone(gsthread_pool_main, top_of_stack, CLONE_VM | CLONE_FILES | SIGCHLD, pool_descr)) < 0) {
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

#define MAX_PROCESS_NAME 17

void
gssetprocessname(char *fmt, ...)
{
#ifdef __linux__
    va_list arg;
    int len;
    char name[MAX_PROCESS_NAME];

    prctl(PR_GET_NAME, name, 0, 0, 0);
    name[MAX_PROCESS_NAME-1] = 0;

    len = strlen(name);
    if (len < MAX_PROCESS_NAME - 1) name[len++] = ':';

    va_start(arg, fmt);
    vseprint(name + len, name + MAX_PROCESS_NAME, fmt, arg);
    va_end(arg);

    prctl(PR_SET_NAME, name, 0, 0, 0);
#endif
}
