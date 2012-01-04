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

static void ibio_unix_fill_stat(char *filename, struct stat *, struct uxio_channel *);

struct uxio_channel *
ibio_sys_stat(char *filename)
{
    struct uxio_channel *chan;
    struct stat uxstat;
    chan = ibio_get_channel_for_external_io(-1, ibio_iostat);
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s:unix stat failed: %r", filename);
    ibio_unix_fill_stat(filename, &uxstat, chan);
    gsfatal("unix ibio_sys_stat(%s) next", filename);
    return chan;
}

static void
ibio_unix_fill_stat(char *filename, struct stat *puxstat, struct uxio_channel *chan)
{
}
