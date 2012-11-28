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
    return (gsvalue)gsunimpl(__FILE__, __LINE__, pos, "gstrue");
}

gsvalue
gsfalse(struct gspos pos)
{
    return (gsvalue)gsunimpl(__FILE__, __LINE__, pos, "gsfalse");
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
gscoerce(gsvalue v, struct gstype *ty, struct gstype **pty, char *err, char *eerr, struct gsfile_symtable *symtable, char *coercion_name, ...)
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

    if (gstypes_type_check(err, eerr, ty->pos, ty, source) < 0)
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
