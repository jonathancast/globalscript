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
    chan->free_end = UXIO_END_OF_IO_BUFFER(buf);

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

static void ibio_alloc_new_uxio_channel_block(void);
static void ibio_alloc_new_uxio_buffer_block(void);

static
struct uxio_channel *
ibio_alloc_uxio_channel()
{
    struct uxio_channel_descr_segment *nursury_seg;
    struct uxio_channel *pres, *pnext;
    if (!uxio_channel_descr_nursury)
        ibio_alloc_new_uxio_channel_block();

    nursury_seg = (struct uxio_channel_descr_segment *)BLOCK_CONTAINING(uxio_channel_descr_nursury);
    pres = (struct uxio_channel *)uxio_channel_descr_nursury;
    pnext = pres + 1;
    if ((uchar*)pnext >= (uchar*)END_OF_BLOCK(nursury_seg))
        ibio_alloc_new_uxio_channel_block();
    else
        uxio_channel_descr_nursury = pnext;

    return pres;
}

static
void
ibio_alloc_new_uxio_channel_block()
{
    struct uxio_channel_descr_segment *nursury_seg;
    nursury_seg = gs_sys_seg_alloc(&uxio_channel_descr);
    uxio_channel_descr_nursury = (void*)((uchar*)nursury_seg + sizeof(*nursury_seg));
    gsassert(!((uintptr)uxio_channel_descr_nursury % sizeof(gsvalue)), "uxio_channel_descr_nursury not gsvalue-aligned; check sizeof(struct uxio_channel_descr_segment");
}

static
void *
ibio_alloc_uxio_buffer()
{
    struct uxio_channel_buffer_segment *nursury_seg;
    void *pres, *pnext;
    if (!uxio_channel_buffer_nursury)
        ibio_alloc_new_uxio_buffer_block();

    nursury_seg = (struct uxio_channel_buffer_segment *)BLOCK_CONTAINING(uxio_channel_buffer_nursury);
    pres = (void *)uxio_channel_buffer_nursury;
    pnext = UXIO_END_OF_IO_BUFFER(pres);
    if ((uchar*)pnext >= (uchar*)END_OF_BLOCK(nursury_seg))
        ibio_alloc_new_uxio_buffer_block();
    else
        uxio_channel_buffer_nursury = pnext;

    return pres;
}

static
void
ibio_alloc_new_uxio_buffer_block(void)
{
    struct uxio_channel_buffer_segment *nursury_seg;
    nursury_seg = gs_sys_seg_alloc(&uxio_channel_buffer);
    uxio_channel_buffer_nursury = (void*)((uchar*)nursury_seg + sizeof(*nursury_seg));
    gsassert(!((uintptr)uxio_channel_buffer_nursury % sizeof(gsvalue)), "uxio_channel_buffer_nursury not gsvalue-aligned; check sizeof(struct uxio_channel_buffer_segment");
}