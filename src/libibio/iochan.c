#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "iosysconstants.h"
#include "iofile.h"

static struct uxio_channel *ibio_alloc_uxio_channel(void);
static void *ibio_alloc_uxio_buffer(void);

struct uxio_channel *
ibio_get_channel_for_external_io(int fd, enum ibio_iochannel_type ty)
{
    struct uxio_channel *chan;
    void *buf;

    chan = ibio_alloc_uxio_channel();
    buf = ibio_alloc_uxio_buffer();

    chan->fd = fd;
    chan->ty = ty;
    chan->free_beg = buf;
    chan->free_end = (uchar*)buf + UXIO_IO_BUFFER_SIZE;

    return chan;
}

struct gs_block_class uxio_channel_descr;
struct gs_block_class uxio_channel_buffer;

struct uxio_channel_descr_segment {
    struct gs_blockdesc desc; /* class = uxio_channel_descr */
};

struct uxio_channel_buffer_segment {
    struct gs_blockdesc desc; /* class = uxio_channel_buffer */
};

void *uxio_channel_descr_nursury;
void *uxio_channel_buffer_nursury;

static
struct uxio_channel *
ibio_alloc_uxio_channel()
{
    gsfatal("ibio_alloc_uxio_channel next");
    return 0;
}

static
void *
ibio_alloc_uxio_buffer()
{
    gsfatal("ibio_alloc_uxio_buffer next");
    return 0;
}

