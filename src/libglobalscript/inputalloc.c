#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputalloc.h"

typedef struct input_block {
    blockheader hdr;
    struct input_block *next;
    void *extent;
} input_block;

static input_block *first_free_block;

static input_block *gs_sys_input_block_alloc();

#define REASONABLE_SIZE_MINIMUM 0x100

#define FREE_SPACE(p) ((uchar*)END_OF_BLOCK(curblock) - (uchar*)curblock->extent)

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
        curblock = curblock->next;
    }

    if (!curblock) {
        curblock = gs_sys_input_block_alloc();
        if (prevblock)
            prevblock->next = curblock;
        else
            first_free_block = curblock;
        if (FREE_SPACE(curblock) < size)
            gsfatal("Allocation too large: %x", size);
    }

    p = curblock->extent;
    curblock->extent = (void*)((uchar*)curblock->extent + size);

    if (FREE_SPACE(curblock) < REASONABLE_SIZE_MINIMUM) {
        if (prevblock)
            prevblock->next = curblock->next;
        else
            first_free_block = curblock->next;
    }

    return p;
}

static
input_block *
gs_sys_input_block_alloc()
{
    input_block *res;

    res = gs_sys_block_alloc(gsinputsection);

    res->next = 0;
    res->extent = START_OF_BLOCK(res);

    return res;
}
