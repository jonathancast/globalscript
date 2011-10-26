#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "../iostat.h"

iptr_t
ibio_sys_stat(char *filename)
{
    gsfatal("unix ibio_sys_stat(%s) next", filename);
    return 0;
}
