#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsheap.h"

typedef struct heapheader {
    void *free_space;
} heapheader;

#define HP_BLOCK_SIZE (BLOCK_SIZE - sizeof(heapheader))

static heapheader *hpblock;
#define hpptr hpblock->free_space

static void gsinitheap(struct gs_blockdesc *, void *);

gsvalue
gseval(gsvalue val)
{
    gsfatal("gseval(%x) next", val);

    return 0;
}

void gsreserveheap(ulong sz)
{
    if (!hpblock)
        hpblock = gs_sys_next_block(0, gsinitheap)
    ;

    if (sz > HP_BLOCK_SIZE)
        gsfatal("Cannot reserve %x bytes of memory; is larger than maximum heap size of %x", sz, HP_BLOCK_SIZE);

    if (hpblock->free_space + sz > END_OF_BLOCK(hpblock))
        hpblock = gs_sys_next_block(0, gsinitheap)
    ;
    while (hpblock->free_space + sz > END_OF_BLOCK(hpblock))
        hpblock = gs_sys_next_block(hpblock, gsinitheap)
    ;
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
