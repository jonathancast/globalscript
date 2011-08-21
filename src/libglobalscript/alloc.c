#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"

static void gs_sys_initialize_curbrk(void);
void *curbrk;

void *gs_sys_block_alloc(registered_block_type ty)
{
    if (!curbrk)
        gs_sys_initialize_curbrk();

    gsfatal("gs_sys_block_alloc(%x) next", ty);
    return 0;
}

static
void
gs_sys_initialize_curbrk()
{
    curbrk = sbrk(0);
    if (curbrk == (void*)-1)
        gsfatal("Could not initialize curbrk: %r");

    if ((uintptr)curbrk % BLOCK_SIZE)
        if (brk((char*)curbrk + BLOCK_SIZE - ((uintptr)curbrk % BLOCK_SIZE)) < 0)
            gsfatal("Allocation failed: %r");
}
