#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

Dir *
ibio_stat(char *filename)
{
    gsfatal("ibio_stat(%s) next", filename);
    return 0;
}
