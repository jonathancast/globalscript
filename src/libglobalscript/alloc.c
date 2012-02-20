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

struct gs_block_class free_block_class_descr = {
    gsnoeval,
};

static void gs_sys_seg_extend(void);
static void gs_sys_seg_setup_free_block(struct free_block *);

void
gs_sys_seg_init(void)
{
    bottom_of_data = sbrk(0);
    if ((uintptr)bottom_of_data % BLOCK_SIZE) {
        bottom_of_data =
            (uchar*)bottom_of_data
            + BLOCK_SIZE
            - ((uintptr)bottom_of_data % BLOCK_SIZE)
        ;
        if (brk(bottom_of_data) < 0)
            gsfatal("brk failed! %r");
    }
    if ((uintptr)bottom_of_data >= GS_MAX_PTR)
        gsfatal("Out of memory in gs_sys_seg_init");

    top_of_data = (uchar*)bottom_of_data + BLOCK_SIZE;
    if (brk(top_of_data) < 0)
        gsfatal("brk failed! %r");

    first_free_block = (struct free_block *)bottom_of_data;
    gs_sys_seg_setup_free_block(first_free_block);
}

void *
gs_sys_seg_alloc(registered_block_class cl)
{
    struct gs_blockdesc *pres;
    struct free_block *pnext;

    if (!bottom_of_data)
        gs_sys_seg_init();

    gsassert(__FILE__, __LINE__, !!first_free_block, "Out of memory");

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
    struct free_block *pnext;

    pnext = (struct free_block *)top_of_data;

    if (brk((uchar*)top_of_data + BLOCK_SIZE) < 0)
        gsfatal("Could not extend data segment: brk failed! %r");
    top_of_data = (uchar*)top_of_data + BLOCK_SIZE;

    gs_sys_seg_setup_free_block(pnext);
    first_free_block = pnext;
}

static
void
gs_sys_seg_setup_free_block(struct free_block *pblock)
{
    pblock->hdr.class = &free_block_class_descr;
    pblock->next = 0;
}
