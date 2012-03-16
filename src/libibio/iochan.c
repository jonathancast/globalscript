#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "iosysconstants.h"
#include "iofile.h"
#include "iomacros.h"

static struct uxio_ichannel *ibio_alloc_uxio_ichannel(void);
static void *ibio_alloc_uxio_buffer(void);
static long uxio_refill_ichan_from_read(struct uxio_ichannel *);

struct uxio_ichannel *
ibio_device_iopen(char *filename, int omode)
{
    int fd;

    omode &= ~(OREAD | OWRITE | ORDWR);
    omode |= OREAD;

    if ((fd = open(filename, omode)) < 0)
        return 0;

    return ibio_get_channel_for_external_io(fd, ibio_ioread);
}

long
ibio_device_iclose(struct uxio_ichannel *chan)
{
    return close(chan->fd);
}

struct uxio_ichannel *
ibio_get_channel_for_external_io(int fd, enum ibio_iochannel_type ty)
{
    struct uxio_ichannel *chan;
    void *buf;

    chan = ibio_alloc_uxio_ichannel();
    buf = ibio_alloc_uxio_buffer();

    chan->fd = fd;
    chan->ty = ty;
    chan->buf_beg = buf;
    chan->free_beg = buf;
    chan->free_end = UXIO_END_OF_IO_BUFFER(buf);

    return chan;
}

struct gs_block_class uxio_channel_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "UXIO Channels",
};
struct gs_block_class uxio_channel_buffer = {
    /* evaluator = */ gsnoeval,
    /* description = */ "UXIO Buffers",
};

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
struct uxio_ichannel *
ibio_alloc_uxio_ichannel()
{
    struct uxio_channel_descr_segment *nursury_seg;
    struct uxio_ichannel *pres, *pnext;

    if (!uxio_channel_descr_nursury)
        ibio_alloc_new_uxio_channel_block();

    nursury_seg = (struct uxio_channel_descr_segment *)BLOCK_CONTAINING(uxio_channel_descr_nursury);
    pres = (struct uxio_ichannel *)uxio_channel_descr_nursury;
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
    gsassert(__FILE__, __LINE__, !((uintptr)uxio_channel_descr_nursury % sizeof(gsvalue)), "uxio_channel_descr_nursury not gsvalue-aligned; check sizeof(struct uxio_channel_descr_segment");
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
    gsassert(__FILE__, __LINE__, !((uintptr)uxio_channel_buffer_nursury % sizeof(gsvalue)), "uxio_channel_buffer_nursury not gsvalue-aligned; check sizeof(struct uxio_channel_buffer_segment");
}

ulong
uxio_channel_size_of_available_data(struct uxio_ichannel *chan)
{
    if ((uchar*)chan->free_end >= (uchar*)chan->free_beg) {
        return ((uchar*)chan->free_beg - (uchar*)chan->buf_beg)
            + ((uchar*)UXIO_END_OF_IO_BUFFER((uchar*)chan->buf_beg)
                - (uchar*)chan->free_end
              )
        ;
    } else {
        return (uchar*)chan->free_beg - (uchar*)chan->free_end;
    }
}

void *
uxio_save_space(struct uxio_ichannel *chan, ulong sz)
{
    void *res;
    int i;
    if (!sz)
        gswarning("Reserving 0 octets (bytes)? on channel %d; resulting pointer will not be vaid so this is probably a concern somebody should track down & look into", chan->fd);
    if ((uchar*)chan->free_end >= (uchar*)chan->free_beg) {
        if (((uchar*)chan->free_end - (uchar*)chan->free_beg) < sz)
            gsfatal("Out of space during read on channel %d", chan->fd);
    } else {
        if (((uchar*)UXIO_END_OF_IO_BUFFER(chan->free_beg) - (uchar*)chan->free_beg) < sz)
            gsfatal("Out of space during read on channel %d", chan->fd);
    }
    res = chan->free_beg;
    chan->free_beg = (uchar*)chan->free_beg + sz;
    for (i = 0; i < sz; i++)
        *((uchar*)res + i) = "\xDE\xAD\xBE\xEF"[i % 4];
    return res;
}

long
uxio_consume_space(struct uxio_ichannel *chan, void *dest, ulong sz)
{
    uchar *p;
    ulong n;

    p = dest, n = sz;
    while (n--) {
        if ((uchar*)chan->free_end < (uchar*)UXIO_END_OF_IO_BUFFER(chan->buf_beg)) {
            *p++ = *(uchar*)chan->free_end;
            chan->free_end = (uchar*)chan->free_end + 1;
        } else if ((uchar*)chan->buf_beg < (uchar*)chan->free_beg) {
            *p++ = *(uchar*)chan->buf_beg;
            chan->free_end = (uchar*)chan->buf_beg + 1;
        } else {
            return sz - n - 1;
        }
        if ((uchar*)chan->free_end == (uchar*)chan->free_beg) {
            chan->free_beg = chan->buf_beg;
            chan->free_end = UXIO_END_OF_IO_BUFFER(chan->buf_beg);
        }
    }
    return sz;
}

long
ibio_device_getline(struct uxio_ichannel *chan, char *dest, long sz)
{
    char *p;
    long n, m;
    char ch;

    p = dest; n = 0;
    while (n < sz) {
        if (
            (char*)chan->free_beg == (char*)chan->buf_beg
            && (char*)chan->free_end == (char*)UXIO_END_OF_IO_BUFFER(chan->buf_beg)
        ) {
            if ((m = uxio_refill_ichan_from_read(chan)) < 0)
                return m;
            if (m == 0) {
                *p++ = 0;
                return n + 1;
            }
        }
        if ((char*)chan->free_end < (char*)UXIO_END_OF_IO_BUFFER(chan->buf_beg)) {
            ch = *(char*)chan->free_end;
            chan->free_end = (uchar*)chan->free_end + 1;
        } else if ((char*)chan->buf_beg < (char*)chan->free_beg) {
            ch = *(char*)chan->buf_beg;
            chan->free_end = (char*)chan->buf_beg + 1;
        } else {
            return n;
        }
        if ((char*)chan->free_end == (char*)chan->free_beg) {
            chan->free_beg = chan->buf_beg;
            chan->free_end = UXIO_END_OF_IO_BUFFER(chan->buf_beg);
        }
        *p++ = ch;
        n++;
        if (ch == '\n' && n < sz) {
            *p++ = 0;
            return n + 1;
        }
    }
    return n;
}

static
char oobuffer[] = "oobuffer: Out of space in buffer on input";

static
long
uxio_refill_ichan_from_read(struct uxio_ichannel *chan)
{
    long n, sz;

    if ((uchar*)chan->free_end < (uchar*)chan->free_beg)
        sz = (uchar*)UXIO_END_OF_IO_BUFFER(chan->buf_beg) - (uchar*)chan->free_beg;
    else
        sz = (uchar*)chan->free_end - (uchar*)chan->free_beg;
    if (sz == 0) {
        werrstr("%s", oobuffer);
        return -1;
    }
    if ((n = read(chan->fd, chan->free_beg, sz)) < 0)
        return n;
    chan->free_beg = (uchar*)chan->free_beg + n;
    if ((uchar*)chan->free_beg == (uchar*)UXIO_END_OF_IO_BUFFER(chan->buf_beg))
        chan->free_beg = chan->buf_beg;
    return n;
}
