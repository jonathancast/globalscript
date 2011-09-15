#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsheap.h"

static void *hpptr;

gstypecode
gseval(gsvalue val)
{
    gsfatal("gseval(%x) next", val);

    return gstyenosys;
}

#define MAXARGS_IN_THUNK 0x100

gsvalue
gsmakethunk(gscode e, ...)
{
    va_list arg;
    gsvalue args[MAXARGS_IN_THUNK], v;
    gsvalue res;
    int i, nargs;

    va_start(arg, e);
    for (i = 0; i < MAXARGS_IN_THUNK; i++) {
        if (!(v = va_arg(arg, gsvalue))) break;
        args[i] = v;
    }
    va_end(arg);

    if (v)
        gsfatal("Too many arguments to gsmakethunk; limit is %x", MAXARGS_IN_THUNK);
    nargs = i;
    
    gsreserveheap(sizeof(gscode) + nargs * sizeof(gsvalue));

    res = (gsvalue)hpptr;
    hpptr = (void*)((uchar*)hpptr + sizeof(gscode) + nargs * sizeof(gsvalue));

    memcpy((uchar*)res, &e, sizeof(gscode));
    memcpy((uchar*)res + sizeof(gscode), args, nargs * sizeof(gsvalue));

    return res;
}
