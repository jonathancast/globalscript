#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "iostat.h"

struct ibio_dir *
ibio_stat(char *filename)
{
    iptr_t buf = ibio_sys_stat(filename);
    return ibio_parse_stat(buf);
}

struct ibio_dir *
ibio_parse_stat(struct uxio_channel * ip)
{
    gsfatal("ibio_parse_stat next");
    return 0;
}
