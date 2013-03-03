/* §source.file Runes */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"
#include "gssetup.h"
#include "ace.h"

static struct gsregistered_primtype rune_prim_types[] = {
    /* name, file, line, group, kind, */
    { "rune", __FILE__, __LINE__, gsprim_type_defined, "u", },
    { 0, },
};

static gsprim_handler rune_prim_handle_advance;

enum {
    rune_prim_advance,
};

static gsprim_handler *rune_prim_exec[] = {
    rune_prim_handle_advance,
};

static gsubprim_handler rune_prim_handle_lt, rune_prim_handle_eq, rune_prim_handle_gt;

enum {
    rune_prim_ub_lt,
    rune_prim_ub_eq,
    rune_prim_ub_gt,
};

static gsubprim_handler *rune_prim_ubexec[] = {
    rune_prim_handle_lt,
    rune_prim_handle_eq,
    rune_prim_handle_gt,
};

static gslprim_handler *rune_prim_lexec[] = {
};

static struct gsregistered_prim rune_prim_operations[] = {
    /* name, file, line, group, apitype, type, index, */
    { "lt", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "rune.prim.rune rune.prim.rune \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", rune_prim_ub_lt, },
    { "eq", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "rune.prim.rune rune.prim.rune \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", rune_prim_ub_eq, },
    { "gt", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "rune.prim.rune rune.prim.rune \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", rune_prim_ub_gt, },
    { "advance", __FILE__, __LINE__, gsprim_operation, 0, "rune.prim.rune natural.prim.u rune.prim.rune → →", rune_prim_advance, },
    { 0, },
};

struct gsregistered_primset gsrune_prim_set = {
    /* name = */ "rune.prim",
    /* types = */ rune_prim_types,
    /* operations = */ rune_prim_operations,
    /* exec_table = */ rune_prim_exec,
    /* ubexec_table = */ rune_prim_ubexec,
    /* lexec_table = */ rune_prim_lexec,
};

static
int
rune_prim_handle_lt(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (args[0] >= args[1])
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ;
}

static
int
rune_prim_handle_eq(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (args[0] != args[1])
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ;
}

static
int
rune_prim_handle_gt(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (args[0] <= args[1])
        return gsprim_return_ubsum(thread, pos, 0, 0)
    ; else
        return gsprim_return_ubsum(thread, pos, 1, 0)
    ;
}

static
int
rune_prim_handle_advance(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args, gsvalue *res)
{
    gsvalue r, n;

    r = args[0];
    n = args[1];

    if (IS_PTR(n)) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "rune_prim_handle_advance: bignum advances next");
        return 0;
    }

    if (!(r & (GS_MAX_PTR >> 1)))
        r &= ~GS_MAX_PTR
    ;
    n &= ~GS_MAX_PTR;

    if (n >= 0x80) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "rune_prim_handle_advance: advance larger than ASCII range");
        return 0;
    }
    if (r >= 0x80 - n) {
        ace_thread_unimpl(thread, __FILE__, __LINE__, pos, "rune_prim_handle_advance: result outside of ASCII range");
        return 0;
    }

    *res = r + n;
    *res |= GS_MAX_PTR;

    return 1;
}

gsvalue
gscstringtogsstring(struct gspos pos, char *s)
{
    char err[0x100];
    gsvalue res;
    struct gsconstr *c;

    c = gsreserveconstrs(utflen(s) * (sizeof(struct gsconstr) + 2 * sizeof(gsvalue)) + sizeof(struct gsconstr));
    res = (gsvalue)c;

    while (*s) {
        struct gsconstr *cnext;

        c->pos = pos;
        c->constrnum = 0;
        c->numargs = 2;
        if (!(s = gschartorune(s, &c->arguments[0], err, err + sizeof(err))))
            gsfatal("%P: gscstringtogsstring: gschartorune failed: %s", pos, err)
        ;
        cnext = (struct gsconstr *)((uchar*)c + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        c->arguments[1] = (gsvalue)cnext;
        c = cnext;
    }
    c->pos = pos;
    c->constrnum = 1;
    c->numargs = 0;

    return res;
}

char *
gschartorune(char *s, gsvalue *pv, char *err, char *eerr)
{
    gsvalue v;
    int nbytes;

    v = 0;

    if (!(*s & 0x80)) {
        nbytes = 1;
    } else if (((uchar)*s & 0xe0) == 0xc0) {
        nbytes = 2;
    } else if (((uchar)*s & 0xf0) == 0xe0) {
        nbytes = 3;
    } else {
        nbytes = 0;
        seprint(err, eerr, UNIMPL("gschartorune(%s)"), s);
        return 0;
    }
    while (nbytes--) {
        v |= (uchar)*s++;
        if (nbytes) v = v << 8;
    }

    v |= GS_MAX_PTR;

    *pv = v;
    return s;
}

char *
gsrunetochar(gsvalue v, char *buf, char *ebuf)
{
    uint mask;
    int test_bit;
    int nbytes;
    int i;

    test_bit = GS_MAX_PTR >> 1;

    if (!(v & test_bit))
        v &= ~GS_MAX_PTR
    ;

    nbytes = 1;
    for (i = 2; i <= 4; i++) {
        mask = 0xff << (8 * (i - 1));
        if (v & mask)
            nbytes = i
        ;
    }
    while (buf < ebuf && nbytes--) {
        mask = 0xff << (8 * nbytes);
        *buf++ = (v & mask) >> (8 * nbytes);
    }
    if (buf < ebuf)
        *buf = 0
    ;

    return buf;
}
