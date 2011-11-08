#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>
#include <libglobalscript.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libibio.h>
#include "iosysconstants.h"
#include "../iofile.h"
#include "../iostat.h"

static void ibio_unix_fill_stat(char *filename, struct stat *, struct ibio_channel *, void *);

iptr_t
ibio_sys_stat(char *filename)
{
    struct ibio_channel *chan;
    void *buf;
    struct stat uxstat;
    chan = ibio_get_channel_for_external_io(ibio_iostat);
    buf = ibio_extend_external_io_buffer(chan, 0x10000);
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s:unix stat failed: %r", filename);
    ibio_unix_fill_stat(filename, &uxstat, chan, buf);
    gsfatal("unix ibio_sys_stat(%s) next", filename);
    return ibio_initial_pos(chan);
}

static void
ibio_unix_fill_stat(char *filename, struct stat *puxstat, struct ibio_channel *chan, void *buf)
{
}
