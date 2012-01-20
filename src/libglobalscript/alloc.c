/* §source.file{The Simple Segment Manager} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"

struct free_block {
    struct gs_blockdesc hdr;
    struct free_block *next;
} free_block;

static void *bottom_of_data, *top_of_data;
static struct free_block *first_free_block;

static void gs_sys_seg_extend(void);

void
gs_sys_seg_init(void)
{
    bottom_of_data = 0;
    gsfatal("gs_sys_seg_init next");
}

void *
gs_sys_seg_alloc(registered_block_class cl)
{
    struct gs_blockdesc *pres;
    struct free_block *pnext;

    if (!bottom_of_data)
        gs_sys_seg_init();

    gsassert(!!first_free_block, "Out of memory");

    pres = &first_free_block->hdr;
    pnext = first_free_block->next;

    if (pnext)
        first_free_block = pnext;
    else
        gs_sys_seg_extend();

    pres->class = cl;

    return (void*)pres;
}

void
gs_sys_seg_free(void *p)
{
    gsfatal("gs_sys_seg_free next");
}

static
void
gs_sys_seg_extend(void)
{
    gsfatal("gs_sys_seg_extend next");
}

