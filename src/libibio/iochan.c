#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "iosysconstants.h"
#include "iofile.h"

struct ibio_channel *
ibio_get_channel_for_external_io(enum ibio_iochannel_type ty)
{
    char s[0x100];
    ibio_efmt_iochannel_type(s, s+sizeof s, ty);
    gsfatal("ibio_get_channel_for_external_io(%d, %s) next", ty, s);
    return 0;
}

void *
ibio_extend_external_io_buffer(struct ibio_channel *chan, long sz)
{
    gsfatal("ibio_extend_external_io_buffer(chan, %x) next", sz);
    return 0;
}

iptr_t
ibio_initial_pos(struct ibio_channel *chan)
{
    gsfatal("ibio_initial_pos next");
    return 0;
}
