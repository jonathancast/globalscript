/* §source.file Client-level Expression Manipulation */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

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
        source = gstype_supply(ty->pos.file, ty->pos.lineno, source, tyarg);
        dest = gstype_supply(ty->pos.file, ty->pos.lineno, dest, tyarg);
    }
    va_end(args);

    if (gstypes_type_check(ty->pos, ty, source, err, eerr) < 0)
        return 0
    ;

    *pty = dest;
    return v; /* No change in representation! */
}
