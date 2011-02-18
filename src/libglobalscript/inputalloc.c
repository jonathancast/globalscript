#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputalloc.h"

void *
gs_sys_input_alloc(unsigned long size)
{
    if (size == 0)
        return 0;

    gsfatal("gs_sys_input_alloc(%lx) next", size);
    return 0;
}
