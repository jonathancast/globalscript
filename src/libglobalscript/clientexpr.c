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
    struct gsconstr_args *true;

    true = gsreserveconstrs(sizeof(*true));
    true->c.pos = pos;
    true->c.type = gsconstr_args;
    true->constrnum = 1;
    true->numargs = 0;

    return (gsvalue)true;
}

gsvalue
gsfalse(struct gspos pos)
{
    struct gsconstr_args *false;

    false = gsreserveconstrs(sizeof(*false));
    false->c.pos = pos;
    false->c.type = gsconstr_args;
    false->constrnum = 0;
    false->numargs = 0;

    return (gsvalue)false;
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

    res = gsreserveheap(MAX(sizeof(struct gsapplication) + n*sizeof(gsvalue), sizeof(struct gsindirection)));

    app = (struct gsapplication *)res;

    res->pos = pos;
    memset(&res->lock, 0, sizeof(res->lock));
    res->type = gsapplication;
    app->fun = fun;
    app->numargs = n;
    for (i = 0; i < n; i++) app->arguments[i] = args[i];

    return (gsvalue)res;
}

gsvalue
gsarraytolist(struct gspos pos, int n, gsvalue *p)
{
    gsvalue res;
    struct gsconstr_args *c;
    int i;

    c = gsreserveconstrs(n * (sizeof(struct gsconstr_args) + 2 * sizeof(gsvalue)) + sizeof(struct gsconstr_args));
    res = (gsvalue)c;

    for (i = 0; i < n; i++) {
        struct gsconstr_args *cnext;

        c->c.pos = pos;
        c->c.type = gsconstr_args;
        c->constrnum = 0;
        c->numargs = 2;
        c->arguments[0] = p[i];
        cnext = (struct gsconstr_args *)((uchar*)c + sizeof(struct gsconstr_args) + 2 * sizeof(gsvalue));
        c->arguments[1] = (gsvalue)cnext;
        c = cnext;
    }
    c->c.pos = pos;
    c->c.type = gsconstr_args;
    c->constrnum = 1;
    c->numargs = 0;

    return res;
}
