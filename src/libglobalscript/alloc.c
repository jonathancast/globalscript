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

void
gs_sys_seg_init(void)
{
    gsfatal("gs_sys_seg_init next");
}

void *
gs_sys_seg_alloc(registered_block_class cl)
{
    gsfatal("gs_sys_seg_alloc next");

    return 0;
}

void
gs_sys_seg_free(void *p)
{
    gsfatal("gs_sys_seg_free next");
}

