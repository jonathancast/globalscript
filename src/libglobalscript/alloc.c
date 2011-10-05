#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"

static void gs_sys_initialize_data_segment(void);
static void gs_sys_expand_data_segment(void);

static gstypecode gsfree(gsvalue, gsvalue *);

typedef struct free_block {
    blockheader hdr;
    struct free_block *prev, *next;
} free_block;

static void *bottom_of_data, *top_of_data;
static free_block *first_free_block;

void *
gs_sys_block_alloc(registered_block_type ty)
{
    blockheader *pblock;

    if (!bottom_of_data)
        gs_sys_initialize_data_segment();

    if (!first_free_block)
        gsfatal("Oops! Ran out of memory");

    if (!first_free_block->next)
        gs_sys_expand_data_segment();

    pblock = (blockheader *)first_free_block;
    first_free_block = first_free_block->next;
    first_free_block->prev = 0;

    pblock->type = ty;

    return pblock;
}

static
void
gs_sys_initialize_data_segment()
{
    top_of_data = sbrk(0);
    if (top_of_data == (void*)-1)
        gsfatal("Could not initialize top_of_data: %r");

    if ((uintptr)top_of_data % BLOCK_SIZE) {
        top_of_data = (char*)top_of_data + BLOCK_SIZE - ((uintptr)top_of_data % BLOCK_SIZE);
        if (brk(top_of_data) < 0)
            gsfatal("Allocation failed: %r");
    }

    bottom_of_data = top_of_data;
    first_free_block = (free_block*)top_of_data;

    if ((uintptr)top_of_data >= GS_MAX_PTR)
        gsfatal("Already reached max available program break during gs_sys_initialize_data_segment");

    top_of_data = (char*)top_of_data + BLOCK_SIZE;
    if (brk(top_of_data) < 0)
        gsfatal("Allocation failed: %r");

    first_free_block->hdr.type = gsfree;
    first_free_block->prev = 0;
    first_free_block->next = 0;
}

static
void
gs_sys_expand_data_segment()
{
    if (first_free_block->next)
        gsfatal("gs_sys_expand_data_segment: Pre-condition failed: first_free_block is not the *last* free block");

    if ((uintptr)top_of_data >= GS_MAX_PTR) {
        gswarning("About to run out of memory");
        gswarning("We should really alert the client so it can do something sensible about now");
        return;
    }

    free_block *last_free_block = top_of_data;

    top_of_data = (char*)top_of_data + BLOCK_SIZE;
    if (brk(top_of_data) < 0)
        gsfatal("Allocation failed: %r");

    last_free_block->hdr.type = gsfree;
    last_free_block->prev = first_free_block;
    last_free_block->next = 0;

    first_free_block->next = last_free_block;

    return;
}

static
gstypecode
gsfree(gsvalue v, gsvalue *pv)
{
    gsfatal("Cannot evaluate free memory");
    return gstyenosys;
}
