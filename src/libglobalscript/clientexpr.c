/* §source.file Client-level Expression Manipulation */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"
#include "gsheap.h"

gsvalue
gstrue(struct gspos pos)
{
    return gsconstr(pos, 1, 0);
}

gsvalue
gsfalse(struct gspos pos)
{
    return gsconstr(pos, 0, 0);
}

gsvalue
gsconstr(struct gspos pos, int constrnum, int numargs, ...)
{
    gsvalue args[MAX_NUM_REGISTERS];
    va_list arg;
    int i;

    va_start(arg, numargs);
    for (i = 0; i < numargs; i++) args[i] = va_arg(arg, gsvalue);
    va_end(arg);

    return gsconstrv(pos, constrnum, numargs, args);
}

gsvalue
gsconstrv(struct gspos pos, int constrnum, int numargs, gsvalue *args)
{
    struct gsconstr *res;
    int i;

    res = gsreserveconstrs(sizeof(*res) + numargs * sizeof(gsvalue));
    res->pos = pos;
    res->type = gsconstr_args;
    res->a.constrnum = constrnum;
    res->a.numargs = numargs;

    for (i = 0; i < numargs; i++) res->a.arguments[i] = args[i];

    return (gsvalue)res;
}

gsvalue
gsemptyrecord(struct gspos pos)
{
    struct gsrecord_fields *res;

    res = gsreserverecords(sizeof(struct gsrecord_fields));
    res->rec.pos = pos;
    res->rec.type = gsrecord_fields;
    res->numfields = 0;

    return (gsvalue)res;
}

gsvalue
gsrecordv(struct gspos pos, int nfields, gsvalue *fields)
{
    struct gsrecord *record;
    struct gsrecord_fields *record_fields;
    int i;

    record_fields = gsreserverecords(sizeof(*record_fields) + nfields * sizeof(gsvalue));
    record = (struct gsrecord *)record_fields;

    record->pos = pos;
    record->type = gsrecord_fields;
    record_fields->numfields = nfields;
    for (i = 0; i < nfields; i++)
        record_fields->fields[i] = fields[i]
    ;
    return (gsvalue)record;
}

gsvalue
gsapply(struct gspos pos, gsvalue fun, gsvalue arg)
{
    return gsnapplyv(pos, fun, 1, &arg);
}

gsvalue
gsnapplyv(struct gspos pos, gsvalue fun, int n, gsvalue *args)
{
    struct gsheap_item *res;
    struct gsapplication *app;
    int i;

    if (n < 1) return fun;

    if (gsisheap_block(BLOCK_CONTAINING(fun))) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)fun;
        lock(&hp->lock);
        if (hp->type == gsclosure) {
            struct gsclosure *cl;
            int needed_args, supplied_args;

            cl = (struct gsclosure *)hp;
            needed_args = cl->cl.code->numargs - (cl->cl.numfvs - cl->cl.code->numfvs);
            supplied_args = MIN(needed_args, n);
            if (needed_args > 0) {
                struct gsclosure *newfun;

                newfun = gsreserveheap(sizeof(struct gsclosure) + (cl->cl.numfvs + supplied_args)*sizeof(gsvalue));
                memset(&newfun->hp.lock, 0, sizeof(newfun->hp.lock));
                newfun->hp.pos = pos;
                newfun->hp.type = gsclosure;
                newfun->cl.code = cl->cl.code;
                newfun->cl.numfvs = cl->cl.numfvs + supplied_args;

                for (i = 0; i < cl->cl.numfvs; i++) newfun->cl.fvs[i] = cl->cl.fvs[i];

                for (i = 0; i < supplied_args; i++) newfun->cl.fvs[cl->cl.numfvs + i] = args[i];

                n -= supplied_args;
                args += supplied_args;
                fun = (gsvalue)newfun;
            }
        }
        unlock(&hp->lock);
    }

    if (n < 1) return fun;

    gsstatprint("%P: Creating application thunk\n", pos);

    res = gsreserveheap(sizeof(struct gsapplication) + n*sizeof(gsvalue));

    app = (struct gsapplication *)res;

    memset(&res->lock, 0, sizeof(res->lock));
    res->pos = pos;
    res->type = gsapplication;
    app->app.fun = fun;
    app->app.numargs = n;
    for (i = 0; i < n; i++) app->app.arguments[i] = args[i];

    return (gsvalue)res;
}

gsvalue
gsarraytolist(struct gspos pos, int n, gsvalue *p)
{
    gsvalue res;
    struct gsconstr *c;
    int i;

    c = gsreserveconstrs(n * (sizeof(struct gsconstr) + 2 * sizeof(gsvalue)) + sizeof(struct gsconstr));
    res = (gsvalue)c;

    for (i = 0; i < n; i++) {
        struct gsconstr *cnext;

        c->pos = pos;
        c->type = gsconstr_args;
        c->a.constrnum = 0;
        c->a.numargs = 2;
        c->a.arguments[0] = p[i];
        cnext = (struct gsconstr *)((uchar*)c + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        c->a.arguments[1] = (gsvalue)cnext;
        c = cnext;
    }
    c->pos = pos;
    c->type = gsconstr_args;
    c->a.constrnum = 1;
    c->a.numargs = 0;

    return res;
}
