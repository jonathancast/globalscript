/* §source.file{The Simple Segment Manager} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"

static Lock gs_allocator_lock;

struct free_block {
    struct gs_blockdesc hdr;
    struct free_block *next;
} free_block;

static void *bottom_of_data, *top_of_data;
static struct free_block *first_free_block;

struct gs_block_class free_block_class_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Free memory",
};

static void _gs_sys_memory_extend(void);
static void gs_sys_block_setup_free_block(struct free_block *);

void
gs_sys_memory_init(void)
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
        gsfatal("Out of memory in gs_sys_memory_init")
    ;

    top_of_data = (uchar*)bottom_of_data + BLOCK_SIZE;
    if (brk(top_of_data) < 0)
        gsfatal("brk failed! %r")
    ;

    first_free_block = (struct free_block *)bottom_of_data;
    gs_sys_block_setup_free_block(first_free_block);
}

/* Needs to lock the program break */
void *
gs_sys_block_alloc(registered_block_class cl)
{
    struct gs_blockdesc *pres;
    struct free_block *pnext;

    lock(&gs_allocator_lock);

    if (!bottom_of_data)
        gs_sys_memory_init()
    ;

    if (!first_free_block)
        gswarning("%s:%d: About to stop going because we're out of memory", __FILE__, __LINE__)
    ;

    pres = &first_free_block->hdr;
    pnext = first_free_block->next;

    if (pnext)
        first_free_block = pnext
    ; else
        _gs_sys_memory_extend()
    ;

    pres->class = cl;

    unlock(&gs_allocator_lock);

    return (void*)pres;
}

/* Assumes the program break is locked */
static
void
_gs_sys_memory_extend(void)
{
    struct free_block *pnext;
    struct gs_blockdesc *pblock;
    void *new_top_of_data;

    pnext = (struct free_block *)top_of_data;

    new_top_of_data = (uchar*)top_of_data + BLOCK_SIZE;
    if ((uintptr)new_top_of_data > GS_MAX_PTR) {
        gswarning("Out of memory in gs_sys_seg_extend; would grow memory past legal maximum extent %ux", GS_MAX_PTR);
        for (pblock = (struct gs_blockdesc *)bottom_of_data; (uchar*)pblock < (uchar*)top_of_data; pblock = (struct gs_blockdesc *)((uchar*)pblock + BLOCK_SIZE)) {
            gswarning("Block %ux is %s", pblock, pblock->class->description);
        }
        gsfatal("Out of memory in gs_sys_seg_extend; would grow memory past legal maximum extent %ux", GS_MAX_PTR);
    }
    if (brk(new_top_of_data) < 0)
        gsfatal("Could not extend data segment: brk failed (allocated %ux to %ux, allocating %x)! %r",
            (uintptr)bottom_of_data,
            (uintptr)top_of_data,
            BLOCK_SIZE
        );
    top_of_data = new_top_of_data;

    gs_sys_block_setup_free_block(pnext);
    first_free_block = pnext;
}

static
void
gs_sys_block_setup_free_block(struct free_block *pblock)
{
    pblock->hdr.class = &free_block_class_descr;
    pblock->next = 0;
}

#define ALIGN_TO(p, align) \
    do { \
        if ((uintptr)(p) % (align)) \
            (p) = (uchar*)(p) + (align) - ((uintptr)(p) % (align)) \
        ; \
    } while (0)

void *
gs_sys_block_suballoc(registered_block_class cl, void **pnursury, ulong sz, ulong align)
{
    struct gs_blockdesc *nursury_block;
    void *res;

    if (!*pnursury) {
        nursury_block = gs_sys_block_alloc(cl);
        *pnursury = START_OF_BLOCK(nursury_block);
        ALIGN_TO(*pnursury, align);
    } else {
        nursury_block = BLOCK_CONTAINING(*pnursury);
    }

    if ((uchar*)*pnursury + sz > (uchar*)END_OF_BLOCK(nursury_block)) {
        nursury_block = gs_sys_block_alloc(cl);
        *pnursury = START_OF_BLOCK(nursury_block);
        ALIGN_TO(*pnursury, align);
    }

    res = *pnursury;
    *pnursury = (uchar*)res + sz;
    ALIGN_TO(*pnursury, align);
    if ((uchar*)*pnursury >= (uchar*)END_OF_BLOCK(nursury_block))
        *pnursury = 0
    ;

    return res;
}

