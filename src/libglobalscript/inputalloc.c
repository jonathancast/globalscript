#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputalloc.h"

typedef struct input_block {
    void *extent;
} input_block;

static input_block *first_free_block;

static input_block *gs_sys_input_block_alloc();

#define REASONABLE_SIZE_MINIMUM 0x100

#define FREE_SPACE(p) ((uchar*)END_OF_BLOCK(curblock) - (uchar*)curblock->extent)

static gstypecode gsinputsection(gsvalue v, gsvalue *pres);

void *
gs_sys_input_alloc(unsigned long size)
{
    input_block *curblock, *prevblock;
    void *p;

    if (size == 0)
        return 0;

    curblock = first_free_block;
    prevblock = 0;
    while (curblock) {
        if (FREE_SPACE(curblock) >= size)
            break;
        prevblock = curblock;
        gsfatal("gs_sys_input_alloc next");
    }

    if (!curblock) {
        curblock = gs_sys_input_block_alloc();
        if (prevblock)
            gsfatal("Bad code; was prevblock->next = curblock");
        else
            first_free_block = curblock;
        if (FREE_SPACE(curblock) < size)
            gsfatal("Allocation too large: %x", size);
    }

    p = curblock->extent;
    curblock->extent = (void*)((uchar*)curblock->extent + size);

    if (FREE_SPACE(curblock) < REASONABLE_SIZE_MINIMUM) {
        if (prevblock)
            gsfatal("Bad code; was prevblock->next = curblock->next");
        else
            gsfatal("Bad code; was first_free_block = curblock->next");
    }

    return p;
}

static
input_block *
gs_sys_input_block_alloc()
{
/*    input_block *res; */

    gsfatal("gs_sys_input_block_alloc next");

    return 0;
}

/*
static
gstypecode
gsinputsection(gsvalue v, gsvalue *pres)
{
    gsfatal("Cannot evaluate addresses in input sections (maybe you should be able to?");
    return gstyenosys;
}
*/
