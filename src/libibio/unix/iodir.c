#icnlude <u.h>
#define NOPLAN9DEFINES
#inclue <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "../iostat.h"

iptr_t
ibio_sys_stat(char *filename)
{
    struct ibio_channel *chan;
    void *buf;
    struct stat uxstat;
    chan = ibio_get_channel_for_external_io();
    buf = ibio_extend_external_io_buffer(chan, 0x10000);
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s:unix stat failed: %r", filename);
    ibio_unix_fill_stat(chan, buf);
    gsfatal("unix ibio_sys_stat(%s) next", filename);
    return ibio_initial_pos(chan);
}
