#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "ace.h"

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
