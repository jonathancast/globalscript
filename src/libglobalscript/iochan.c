#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "iosysconstants.h"
#include "iofile.h"
#include "iomacros.h"

static struct uxio_ichannel *gsbio_alloc_uxio_ichannel(void);
static void *gsbio_alloc_uxio_buffer(void);
static long uxio_refill_ichan_from_read(struct uxio_ichannel *);

struct uxio_ichannel *
gsbio_device_iopen(char *filename, int omode)
{
    int fd;
    char *nm;

    omode &= ~(OREAD | OWRITE | ORDWR);
    omode |= OREAD;

    if ((fd = open(filename, omode)) < 0)
        return 0;

    nm = gs_sys_block_suballoc(
        &uxio_filename_class,
        &uxio_filename_nursury,
        strlen(filename) + 1,
        1
    );
    strcpy(nm, filename);

    return gsbio_get_channel_for_external_io(nm, fd, gsbio_ioread);
}

long
gsbio_device_iclose(struct uxio_ichannel *chan)
{
    if (chan->fd < 0)
        return 0;

    return close(chan->fd);
}

struct uxio_ichannel *
gsbio_get_channel_for_external_io(char *filename, int fd, enum gsbio_iochannel_type ty)
{
    struct uxio_ichannel *chan;
    void *buf;

    chan = gsbio_alloc_uxio_ichannel();
    buf = gsbio_alloc_uxio_buffer();

    chan->fd = fd;
    chan->filename = filename;
    chan->ty = ty;
    chan->at_eof = 0;
    chan->buf_beg = buf;
    chan->data_beg = buf;
    chan->data_end = buf;
    chan->offset = 0;

    return chan;
}

struct gs_block_class uxio_channel_buffer = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "UXIO Buffers",
};

struct uxio_channel_buffer_segment {
    struct gs_blockdesc desc; /* class = uxio_channel_buffer */
};

struct gs_block_class uxio_channel_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "UXIO Channels",
};
static void *uxio_channel_descr_nursury;
static Lock uxio_channel_descr_lock;

void *uxio_channel_buffer_nursury;

static void gsbio_alloc_new_uxio_buffer_block(void);

static
struct uxio_ichannel *
gsbio_alloc_uxio_ichannel()
{
    struct uxio_ichannel *res;

    lock(&uxio_channel_descr_lock);
    res = gs_sys_block_suballoc(&uxio_channel_descr, &uxio_channel_descr_nursury, sizeof(*res), sizeof(void*));
    unlock(&uxio_channel_descr_lock);

    return res;
}

static
void *
gsbio_alloc_uxio_buffer()
{
    struct uxio_channel_buffer_segment *nursury_seg;
    void *pres, *pnext;

    if (!uxio_channel_buffer_nursury)
        gsbio_alloc_new_uxio_buffer_block();

    nursury_seg = (struct uxio_channel_buffer_segment *)BLOCK_CONTAINING(uxio_channel_buffer_nursury);
    pres = (void *)uxio_channel_buffer_nursury;
    pnext = UXIO_END_OF_IO_BUFFER(pres);
    if ((uchar*)pnext >= (uchar*)END_OF_BLOCK(nursury_seg))
        gsbio_alloc_new_uxio_buffer_block();
    else
        uxio_channel_buffer_nursury = pnext;

    return pres;
}

static
void
gsbio_alloc_new_uxio_buffer_block(void)
{
    struct uxio_channel_buffer_segment *nursury_seg;
    nursury_seg = gs_sys_block_alloc(&uxio_channel_buffer);
    uxio_channel_buffer_nursury = (void*)((uchar*)nursury_seg + sizeof(*nursury_seg));
    gsassert(__FILE__, __LINE__, !((uintptr)uxio_channel_buffer_nursury % sizeof(gsvalue)), "uxio_channel_buffer_nursury not gsvalue-aligned; check sizeof(struct uxio_channel_buffer_segment");
}

ulong
uxio_channel_size_of_available_data(struct uxio_ichannel *chan)
{
    return (uchar*)chan->data_end - (uchar*)chan->data_beg;
}

void *
uxio_save_space(struct uxio_ichannel *chan, ulong sz)
{
    void *res;
    int i;
    if (!sz)
        gswarning("Reserving 0 octets (bytes)? on channel %d; resulting pointer will not be vaid so this is probably a concern somebody should track down & look into", chan->fd);
    if (((uchar*)UXIO_END_OF_IO_BUFFER(chan->buf_beg) - (uchar*)chan->data_end) < sz)
        gsfatal("Out of space during read on channel %d", chan->fd);
    res = chan->data_end;
    chan->data_end = (uchar*)chan->data_end + sz;
    for (i = 0; i < sz; i++)
        *((uchar*)res + i) = "\xDE\xAD\xBE\xEF"[i % 4];
    return res;
}

long
uxio_consume_space(struct uxio_ichannel *chan, void **dest, ulong sz)
{
    ulong n;

    *dest = chan->data_beg;
    n = (uchar*)chan->data_end - (uchar*)chan->data_beg;
    if (sz < n) {
        chan->data_beg = (uchar*)chan->data_beg + sz;
        return sz;
    } else {
        chan->data_beg = chan->data_end = chan->buf_beg;
        return n;
    }
}

long
gsbio_get_contents(struct uxio_ichannel *chan, char *buf, long max)
{
    char *p;
    long n, m;

    p = buf, n = 0;
    while (n < max) {
        if ((char*)chan->data_end == (char*)chan->data_beg) {
            if ((m = uxio_refill_ichan_from_read(chan)) < 0)
                return m;
            if (m == 0) {
                *p++ = 0;
                return n + 1;
            }
        }
        *p++ = *(char*)chan->data_beg;
        chan->data_beg = (char*)chan->data_beg + 1;
        n++;
        if ((uchar*)chan->data_end == (uchar*)chan->data_beg) {
            chan->data_beg = chan->data_end = chan->buf_beg;
        }
    }
    return n;
}

long
gsbio_device_getline(struct uxio_ichannel *chan, char *dest, long sz)
{
    char *p;
    long n, m;
    char ch;

    p = dest; n = 0;
    while (n < sz) {
        if ((char*)chan->data_end == (char*)chan->data_beg) {
            if ((m = uxio_refill_ichan_from_read(chan)) < 0)
                return m;
            if (m == 0) {
                *p++ = 0;
                return n + 1;
            }
        }
        ch = *(char*)chan->data_beg;
        chan->data_beg = (uchar*)chan->data_beg + 1;
        if ((char*)chan->data_end == (char*)chan->data_beg) {
            chan->data_beg = chan->data_end = chan->buf_beg;
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

    if (chan->fd < 0)
        return 0
    ;
    sz = (uchar*)UXIO_END_OF_IO_BUFFER(chan->buf_beg) - (uchar*)chan->data_end;
    if (sz == 0) {
        werrstr("%s", oobuffer);
        return -1;
    }
    if ((n = read(chan->fd, chan->data_end, sz)) < 0)
        return n;
    chan->data_end = (uchar*)chan->data_end + n;
    chan->at_eof = (n == 0);
    return n;
}

int
gsbio_idevice_at_eof(struct uxio_ichannel *chan)
{
    return chan->at_eof;
}
