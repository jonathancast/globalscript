#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "ace.h"

struct gsbc_cont_update *
ace_push_update(struct gspos pos, struct ace_thread *thread, struct gsheap_item *hp)
{
    struct gsbc_cont *cont;
    struct gsbc_cont_update *updatecont;

    cont = ace_stack_alloc(thread, hp->pos, sizeof(struct gsbc_cont_update));
    updatecont = (struct gsbc_cont_update *)cont;
    if (!cont) {
        gsupdate_heap(hp, (gsvalue)gsunimpl(__FILE__, __LINE__, pos, "Out of stack space allocating update continuation"));
        gsheap_unlock(hp);
        return 0;
    }
    cont->node = gsbc_cont_update;
    cont->pos = hp->pos;
    updatecont->dest = hp;
    updatecont->next = thread->cureval;
    thread->cureval = updatecont;
    return updatecont;
}

struct gsbc_cont_app *
ace_push_app(struct gspos pos, struct ace_thread *thread, int numargs, ...)
{
    va_list arg;
    int i;
    gsvalue arguments[MAX_NUM_REGISTERS];

    va_start(arg, numargs);
    for (i = 0; i < numargs; i++) arguments[i] = va_arg(arg, gsvalue);
    va_end(arg);

    return ace_push_appv(pos, thread, numargs, arguments);
}

struct gsbc_cont_app *
ace_push_appv(struct gspos pos, struct ace_thread *thread, int numargs, gsvalue *arguments)
{
    int i;
    struct gsbc_cont *cont;
    struct gsbc_cont_app *appcont;

    cont = ace_stack_alloc(thread, pos, sizeof(struct gsbc_cont_app) + numargs * sizeof(gsvalue));
    appcont = (struct gsbc_cont_app *)cont;
    if (!cont) {
        ace_failure_thread(thread, gsunimpl(__FILE__, __LINE__, pos, "Out of stack space allocating app continuation"));
        return 0;
    }

    cont->node = gsbc_cont_app;
    cont->pos = pos;
    appcont->numargs = numargs;
    for (i = 0; i < numargs; i++) appcont->arguments[i] = arguments[i];

    return appcont;
}

struct gsbc_cont *
ace_stack_alloc(struct ace_thread *thread, struct gspos pos, ulong sz)
{
    void *newtop;

    newtop = (uchar*)thread->stacktop - sz;
    if ((uintptr)newtop % sizeof(gsvalue)) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack mis-aligned (can't round down or we couldn't pop properly)");
        return 0;
    }

    if ((uchar*)newtop < (uchar*)thread->stacklimit) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack overflow");
        return 0;
    }

    thread->stacktop = newtop;

    return (struct gsbc_cont *)newtop;
}

struct gsbc_cont *
ace_stack_top(struct ace_thread *thread)
{
    if ((uchar*)thread->stacktop < (uchar*)thread->stackbot)
        return (struct gsbc_cont *)thread->stacktop
    ; else
        return 0
    ;
}

void
ace_pop_update(struct ace_thread *thread)
{
    struct gsbc_cont_update *update;

    update = (struct gsbc_cont_update *)ace_stack_top(thread);

    thread->cureval = update->next;
    thread->stacktop = (uchar*)update + sizeof(struct gsbc_cont_update);
}
