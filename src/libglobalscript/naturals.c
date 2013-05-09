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
    natural_prim_plus,
};

static gsprim_handler natural_prim_handle_plus;

static gsprim_handler *natural_prim_exec[] = {
    natural_prim_handle_plus,
};

enum {
    natural_prim_ub_minus,
    natural_prim_ub_divMod,
    natural_prim_ub_eq,
    natural_prim_ub_lt,
    natural_prim_ub_gt,
};

static gsubprim_handler natural_prim_handle_minus, natural_prim_handle_divMod;
static gsubprim_handler natural_prim_handle_eq, natural_prim_handle_lt, natural_prim_handle_gt;

static gsubprim_handler *natural_prim_ubexec[] = {
    natural_prim_handle_minus,
    natural_prim_handle_divMod,

    natural_prim_handle_eq,
    natural_prim_handle_lt,
    natural_prim_handle_gt,
};

static gslprim_handler *natural_prim_lexec[] = {
};

static struct gsregistered_prim natural_prim_operations[] = {
    /* name, file, line, group, apitype, type, index, */
    { "+", __FILE__, __LINE__, gsprim_operation, 0, "natural.prim.u natural.prim.u natural.prim.u → →", natural_prim_plus, },
    { "-", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 natural.prim.u \"uΣ〈 0 1 〉 → →", natural_prim_ub_minus, },
    { "divMod", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 natural.prim.u natural.prim.u \"uΠ〈 0 1 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_divMod, },

    { "≡", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_eq, },
    { "<", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_lt, },
    { ">", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "natural.prim.u natural.prim.u \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", natural_prim_ub_gt, },

    { 0, },
};

struct gsregistered_primset gsnatural_prim_set = {
    /* name = */ "natural.prim",
    /* types = */ natural_prim_types,
    /* operations = */ natural_prim_operations,
    /* exec_table = */ natural_prim_exec,
    /* ubexec_table = */ natural_prim_ubexec,
    /* lexec_table = */ natural_prim_lexec,
};

/* §section Arithmetic Primitives */

static
int
natural_prim_handle_plus(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args, gsvalue *pres)
{
    gsvalue addend0, addend1, sum;

    addend0 = args[0];
    addend1 = args[1];
    if (IS_PTR(addend0) || IS_PTR(addend1))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_plus: bignums")
    ;

    addend0 &= ~GS_MAX_PTR;
    addend1 &= ~GS_MAX_PTR;

    if (addend0 >= GS_MAX_PTR - addend1)
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_plus: result is a bignum")
    ;

    sum = addend0 + addend1;
    sum |= GS_MAX_PTR;

    *pres = sum;
    return 1;
}

static
int
natural_prim_handle_minus(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    gsvalue minuend, subtrahend, difference;

    minuend = args[0];
    subtrahend = args[1];

    if (IS_PTR(minuend) || IS_PTR(subtrahend))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_minus: bignums")
    ;

    minuend &= ~GS_MAX_PTR;
    subtrahend &= ~GS_MAX_PTR;

    if (minuend < subtrahend)
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ;

    difference = minuend - subtrahend;
    difference |= GS_MAX_PTR;

    return gsprim_return_ubsum(thread, pos, 1, 1, difference);
}

static
int
natural_prim_handle_divMod(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    gsvalue dividend, divisor;

    dividend = args[0];
    divisor = args[1];
    if (IS_PTR(dividend) || IS_PTR(divisor))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_divMod: bignums")
    ;

    dividend &= ~GS_MAX_PTR;
    divisor &= ~GS_MAX_PTR;

    if (divisor == 0)
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ; else {
        gsvalue quotient, remainder;

        quotient = dividend / divisor;
        remainder = dividend % divisor;
        quotient |= GS_MAX_PTR;
        remainder |= GS_MAX_PTR;
        return gsprim_return_ubsum(thread, pos, 1, 2, quotient, remainder);
    }
}

/* §section Ordering Primitives */

static
int
natural_prim_handle_eq(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (IS_PTR(args[0]) || IS_PTR(args[1]))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_eq: bignums")
    ;
    if (args[0] == args[1])
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ;
}

static
int
natural_prim_handle_lt(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (IS_PTR(args[0]) || IS_PTR(args[1]))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_lt: bignums")
    ;
    if (args[0] < args[1])
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ;
}

static
int
natural_prim_handle_gt(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (IS_PTR(args[0]) || IS_PTR(args[1]))
        return gsprim_unimpl(thread, __FILE__, __LINE__, pos, "natural_prim_handle_gt: bignums")
    ;
    if (args[0] > args[1])
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ;
}

/* §section C-level Functions */

char *
gsnaturaltochar(char *err, char *eerr, gsvalue v, char *buf, char *ebuf)
{
    if (IS_PTR(v)) {
        seprint(err, eerr, "Cannot print %p; is a pointer", v);
        return 0;
    }

    v &= ~GS_MAX_PTR;

    return seprint(buf, ebuf, "%ud", v);
}
