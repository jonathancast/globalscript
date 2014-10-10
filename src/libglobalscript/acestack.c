#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "ace.h"

struct gsbc_cont_update *
ace_push_update(struct gspos pos, struct ace_thread *thread, struct gsheap_item *hp)
{
    struct ace_cont *cont;
    struct gsbc_cont_update *updatecont;
    struct gsindirection *in;

    cont = ace_stack_alloc(thread, hp->pos, sizeof(struct gsbc_cont_update));
    updatecont = (struct gsbc_cont_update *)cont;
    if (!cont) {
        struct gsimplementation_failure *err;

        err = gsunimpl(__FILE__, __LINE__, pos, "Out of stack space allocating update continuation");
        hp->type = gsindirection;
        in = (struct gsindirection *)hp;
        in->dest = (gsvalue)err;
        gsheap_unlock(hp);
        ace_failure_thread(thread, err);
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
    struct ace_cont *cont;
    struct gsbc_cont_app *appcont;

    cont = ace_stack_alloc(thread, pos, sizeof(struct gsbc_cont_app) + numargs * sizeof(gsvalue));
    if (!cont) return 0;
    appcont = (struct gsbc_cont_app *)cont;

    cont->node = gsbc_cont_app;
    cont->pos = pos;
    appcont->numargs = numargs;
    for (i = 0; i < numargs; i++) appcont->arguments[i] = arguments[i];

    return appcont;
}

/* Shuts down the thread if anything goes wrong */
struct ace_cont *
ace_stack_alloc(struct ace_thread *thread, struct gspos pos, ulong sz)
{
    void *newtop;

    newtop = (uchar*)thread->stacktop - sz;
    if ((uintptr)newtop % sizeof(gsvalue)) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack mis-aligned (can't round down or we couldn't pop properly)");
        return 0;
    }

    if ((uchar*)newtop < (uchar*)thread->stacklimit) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "stack overflow in %P", thread->cureval->cont.pos);
        return 0;
    }

    thread->stacktop = newtop;

    return (struct ace_cont *)newtop;
}

struct ace_cont *
ace_stack_top(struct ace_thread *thread)
{
    if ((uchar*)thread->stacktop < (uchar*)thread->stackbot)
        return (struct ace_cont *)thread->stacktop
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

int
ace_stack_gcevacuate(struct gsstringbuilder *err, struct ace_thread *thread, struct gsbc_cont_update *update)
{
    void *p, *top, *base;
    int i;
    gsvalue gctemp;

    top = thread->gc_evacuated_stackbot;
    base = (uchar*)update + sizeof(*update);

    if ((uchar*)base <= (uchar*)top) return 0;

    thread->gc_evacuated_stackbot = base;

    for (p = top; (uchar*)p < (uchar*)base; ) {
        struct ace_cont *cont = (struct ace_cont *)p;

        if (gs_gc_trace_pos(err, &cont->pos) < 0) return -1;

        switch (cont->node) {
            case gsbc_cont_update: {
                update = (struct gsbc_cont_update *)cont;

                p = (uchar*)update + sizeof(*update);
                continue;
            }
            case gsbc_cont_app: {
                struct gsbc_cont_app *app = (struct gsbc_cont_app *)cont;

                for (i = 0; i < app->numargs; i++)
                    if (GS_GC_TRACE(err, &app->arguments[i]) < 0) return -1
                ;

                p = (uchar*)app + sizeof(*app) + app->numargs * sizeof(gsvalue);
                continue;
            }
            case gsbc_cont_strict: {
                struct gsbc_cont_strict *strict = (struct gsbc_cont_strict *)cont;

                if (gs_gc_trace_bco(err, &strict->code) < 0) return -1;

                for (i = 0; i < strict->numfvs; i++)
                    if (GS_GC_TRACE(err, &strict->fvs[i]) < 0) return -1
                ;

                p = (uchar*)strict + sizeof(*strict) + strict->numfvs * sizeof(gsvalue);

                continue;
            }
            case gsbc_cont_force: {
                struct gsbc_cont_force *force = (struct gsbc_cont_force *)cont;

                if (gs_gc_trace_bco(err, &force->code) < 0) return -1;

                for (i = 0; i < force->numfvs; i++)
                    if (GS_GC_TRACE(err, &force->fvs[i]) < 0) return -1
                ;

                p = (uchar*)force + sizeof(*force) + force->numfvs * sizeof(gsvalue);
                continue;
            }
/* ↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓ */
/* Auto-generated by mdltype2c */
/* Do not change if West Baiýan Naval Intelligence knows where you live */

            case ace_stack_ubanalyze_cont: {
                struct ace_stack_ubanalyze_cont *ubanalyze = (struct ace_stack_ubanalyze_cont *)cont;
                for (i = 0; i < ubanalyze->numconts; i++)
                    if (gs_gc_trace_bco(err, &ACE_STACK_UBANALYZE_CONT(*ubanalyze, i)) < 0) return -1
                ;
                for (i = 0; i < ubanalyze->numfvs; i++)
                    if (GS_GC_TRACE(err, &ACE_STACK_UBANALYZE_FV(*ubanalyze, i)) < 0) return -1
                ;
                p = ACE_STACK_UBANALYZE_BOTTOM(ubanalyze);
                continue;
            }

/* ↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑ */
            default:
                gsstring_builder_print(err, UNIMPL("ace_eval_gc_trace: evacuate stack (node = %d)"), cont->node);
                return -1;
        }
    }

    return 0;
}

int
ace_stack_post_gc_consolidate(struct gsstringbuilder *err, struct ace_thread *thread)
{
    void *dest;
    void *src, *srctop;
    ulong sz;
    struct gsbc_cont_update *upupdate, *update, *downupdate;
    int is_live;

    upupdate = 0;
    for (update = thread->cureval; update; update = downupdate) {
        downupdate = update->next;
        update->next = upupdate;
        upupdate = update;
    }

    downupdate = 0;
    dest = src = thread->stackbot;
    is_live = 0;
    for (update = upupdate; update; update = upupdate) {
        upupdate = update->next;
        update->next = downupdate;

        srctop = update;
        sz = (uchar*)src - (uchar*)srctop;

        if (!gs_sys_block_in_gc_from_space(update->dest)) {
            gsstring_builder_print(err, UNIMPL("%P: Trace update with live dest"), update->cont.pos);
            return -1;
        } else if (update->dest->type == gsgcforward) {
            update->dest = (struct gsheap_item *)((struct gsgcforward *)update->dest)->gcfwd;

            /* §paragraph{Copy update frame up on the stack} */
            dest = (uchar*)dest - sz;
            memmove(dest, srctop, sz);
            src = (uchar*)src - sz;
            downupdate = (struct gsbc_cont_update *)dest;

            is_live = 1;
        } else {
            /* §paragraph{Skip update frame} */
            src = (uchar*)src - sz;
        }
        /* §paragraph{Move src down to next update frame or stack top; copy if we've entered the live portion of the stack} */
        srctop = upupdate ? (uchar*)upupdate + sizeof(*upupdate) : (uchar*)thread->stacktop;
        sz = (uchar*)src - (uchar*)srctop;
        if (is_live) {
            dest = (uchar*)dest - sz;
            if (sz) memmove(dest, srctop, sz);
        }
        src = (uchar*)src - sz;
    }

    if (thread->state == ace_thread_entering_bco) {
        int numfvs;
        void *srcret, *destret;

        numfvs = thread->st.entering_bco.bco->numfvs + thread->st.entering_bco.bco->numargs;
        srcret = (uchar*)src - numfvs * sizeof(gsvalue);
        destret = (uchar*)dest - numfvs * sizeof(gsvalue);

        memmove(destret, srcret, numfvs * sizeof(gsvalue));
    } else if (thread->state == ace_thread_returning) {
        struct ace_cont *cont = (struct ace_cont *)thread->stacktop;

        switch (cont->node) {
            case gsbc_cont_update:
            case gsbc_cont_app:
            case gsbc_cont_force: {
                void *srcret, *destret;

                srcret = (uchar*)src - sizeof(gsvalue);
                destret = (uchar*)dest - sizeof(gsvalue);

                memmove(destret, srcret, sizeof(gsvalue));

                break;
            }
            case ace_stack_ubanalyze_cont: {
                struct ace_stack_ubanalyze_cont *ubanalyze = (struct ace_stack_ubanalyze_cont *)cont;
                void *srcret, *destret;

                srcret = (uchar*)src - ACE_STACK_UBANALYZE_ARGS_SIZE(thread, ubanalyze);
                destret = (uchar*)dest - ACE_STACK_UBANALYZE_ARGS_SIZE(thread, ubanalyze);

                memmove(destret, srcret, ACE_STACK_UBANALYZE_ARGS_SIZE(thread, ubanalyze));

                break;
            }
            default:
                gsstring_builder_print(err, UNIMPL("%P: Move result: node = %d"), cont->pos, cont->node);
                return -1;
        }
    }

    thread->cureval = downupdate;
    thread->stacktop = dest;

    return 0;
}
