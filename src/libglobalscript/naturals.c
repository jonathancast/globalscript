/* §source.file Natural Numbers */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gssetup.h"

static struct gsregistered_primtype natural_prim_types[] = {
    /* name, file, line, group, kind, */
    { "natural", __FILE__, __LINE__, gsprim_type_defined, "u", },
    { 0, },
};

enum {
    natural_prim_ub_eq,
    natural_prim_ub_lt,
};

static struct gsregistered_prim natural_prim_operations[] = {
    /* name, file, line, group, apitype, type, index, */
    { "≡", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_eq, },
    { "<", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_lt, },
    { 0, },
};

static gsubprim_handler natural_prim_handle_eq, natural_prim_handle_lt;

static gsubprim_handler *natural_prim_ubexec[] = {
    natural_prim_handle_eq,
    natural_prim_handle_lt,
};

struct gsregistered_primset gsnatural_prim_set = {
    /* name = */ "natural.prim",
    /* types = */ natural_prim_types,
    /* operations = */ natural_prim_operations,
    /* ubexec_table = */ natural_prim_ubexec,
};

static
int
natural_prim_handle_eq(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (
        GS_SLOW_EVALUATE(args[0]) != gstyunboxed
        || GS_SLOW_EVALUATE(args[1]) != gstyunboxed
    )
        return gsubprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_eq: bignums")
    ;
    if (args[0] == args[1])
        return gsubprim_return(thread, pos, 1, 0)
    ; else
        return gsubprim_return(thread, pos, 0, 0)
    ;
}

static
int
natural_prim_handle_lt(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (
        GS_SLOW_EVALUATE(args[0]) != gstyunboxed
        || GS_SLOW_EVALUATE(args[1]) != gstyunboxed
    )
        return gsubprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_lt: bignums")
    ;
    if (args[0] < args[1])
        return gsubprim_return(thread, pos, 1, 0)
    ; else
        return gsubprim_return(thread, pos, 0, 0)
    ;
}

char *
gsnaturaltochar(char *err, char *eerr, gsvalue v, char *buf, char *ebuf)
{
    int len;
    gsvalue ubound;

    if (IS_PTR(v)) {
        seprint(err, eerr, "Cannot print %p; is a pointer", v);
        return 0;
    }

    v &= ~GS_MAX_PTR;

    len = 1;
    ubound = 10;

    while (ubound < GS_MAX_PTR / 10 && ubound <= v)
        len++, ubound *= 10
    ;

    if (ebuf - buf - 1 < len) {
        seprint(err, eerr, "Cannot print %p; has %d digits but only have space for %d", len, ebuf - buf - 1);
        return 0;
    }

    while (len--) {
        ubound = ubound / 10;
        *buf++ = '0' + v % ubound;
    }

    *buf++ = 0;

    return buf;
}
