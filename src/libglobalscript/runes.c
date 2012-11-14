/* §source.file Runes */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsheap.h"

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

    v = 0;

    if (!*s) {
    } else if (!(*s & 0x80)) {
        v = (uchar)*s++;
    } else {
        seprint(err, eerr, "%s:%d: gschartorune(%s) next", __FILE__, __LINE__, s);
        return 0;
    }

    v |= GS_MAX_PTR;

    *pv = v;
    return s;
}

char *
gsrunetochar(gsvalue v, char *buf, char *ebuf, char *err, char *eerr)
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
