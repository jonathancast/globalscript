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
    struct gsconstr *true;

    true = gsreserveconstrs(sizeof(*true));
    true->pos = pos;
    true->constrnum = 1;
    true->numargs = 0;

    return (gsvalue)true;
}

gsvalue
gsfalse(struct gspos pos)
{
    struct gsconstr *false;

    false = gsreserveconstrs(sizeof(*false));
    false->pos = pos;
    false->constrnum = 0;
    false->numargs = 0;

    return (gsvalue)false;
}

gsvalue
gsemptyrecord(struct gspos pos)
{
    struct gsrecord *res;

    res = gsreserverecords(sizeof(struct gsrecord));
    res->pos = pos;
    res->numfields = 0;

    return (gsvalue)res;
}

gsvalue
gsrecordv(struct gspos pos, int nfields, gsvalue *fields)
{
    struct gsrecord *record;
    int i;

    record = gsreserverecords(sizeof(*record) + nfields * sizeof(gsvalue));

    record->pos = pos;
    record->numfields = nfields;
    for (i = 0; i < nfields; i++)
        record->fields[i] = fields[i]
    ;
    return (gsvalue)record;
}

gsvalue
gscoerce(gsvalue v, struct gstype *ty, struct gstype **pty, struct gsstringbuilder *err, struct gsfile_symtable *symtable, char *coercion_name, ...)
{
    struct gsbc_coercion_type *ct;
    va_list args;
    struct gstype *source, *dest, *tyarg;

    ct = gssymtable_get_coercion_type(symtable, gsintern_string(gssymcoercionlable, coercion_name));

    if (!ct) {
        werrstr("No such coercion %s", coercion_name);
        return 0;
    }

    source = ct->source;
    dest = ct->dest;

    va_start(args, coercion_name);
    while (tyarg = va_arg(args, struct gstype *)) {
        source = gstype_apply(ty->pos, source, tyarg);
        dest = gstype_apply(ty->pos, dest, tyarg);
    }
    va_end(args);

    if (gstypes_type_check(err, ty->pos, ty, source) < 0)
        return 0
    ;

    *pty = dest;
    return v; /* No change in representation! */
}

gsvalue
gsapply(struct gspos pos, gsvalue fun, gsvalue arg)
{
    struct gsheap_item *res;
    struct gsapplication *app;

    res = gsreserveheap(MAX(sizeof(struct gsapplication) + 1*sizeof(gsvalue), sizeof(struct gsindirection)));

    app = (struct gsapplication *)res;

    res->pos = pos;
    res->type = gsapplication;
    app->fun = fun;
    app->numargs = 1;
    app->arguments[0] = arg;

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
        c->constrnum = 0;
        c->numargs = 2;
        c->arguments[0] = p[i];
        cnext = (struct gsconstr *)((uchar*)c + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        c->arguments[1] = (gsvalue)cnext;
        c = cnext;
    }
    c->pos = pos;
    c->constrnum = 1;
    c->numargs = 0;

    return res;
}
