/* §source.file Natural Numbers */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

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
