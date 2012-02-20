#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "iosysconstants.h"
#include "iofile.h"
#include "iostat.h"
#include "iomacros.h"

static struct ibio_dir *ibio_alloc_dir(ulong size);

struct ibio_dir *
ibio_stat(char *filename)
{
    struct uxio_channel *chan = ibio_sys_stat(filename);
    return ibio_parse_stat(chan);
}

struct ibio_dir *
ibio_parse_stat(struct uxio_channel *ip)
{
    struct ibio_dir dir, *res;
    void *resend;
    uchar buf[2];
    long n;
    u16int bufsize;

    memset(&dir, 0, sizeof(dir));

    /* size */
    if ((n = uxio_consume_space(ip, &buf, 2)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 2)
        gsfatal("%s:%d: Couldn't get size field; got %x octets", __FILE__, __LINE__, n);
    bufsize = GET_LITTLE_ENDIAN_U16INT(buf);
    gsassert_ulong_le(__FILE__, __LINE__, bufsize, uxio_channel_size_of_available_data(ip), "Reported size larger than available data");

    /* type */
    if ((n = uxio_consume_space(ip, &buf, 2)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 2)
        gsfatal("%s:%d: Couldn't get type field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 2;

    /* dev */
    if ((n = uxio_consume_space(ip, &buf, 4)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 4)
        gsfatal("%s:%d: Couldn't get dev field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 4;

    /* qid.type */
    if ((n = uxio_consume_space(ip, &buf, 1)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 1)
        gsfatal("%s:%d: Couldn't get qid.type field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 1;

    /* qid.vers */
    if ((n = uxio_consume_space(ip, &buf, 4)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 4)
        gsfatal("%s:%d: Couldn't get qid.vers field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 4;

    /* qid.path */
    if ((n = uxio_consume_space(ip, &buf, 8)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 8)
        gsfatal("%s:%d: Couldn't get qid.path field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 8;

    /* mode */
    if ((n = uxio_consume_space(ip, &buf, 4)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 4)
        gsfatal("%s:%d: Couldn't get mode field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 4;
    dir.d.mode = GET_LITTLE_ENDIAN_U32INT(buf);

    dir.size = sizeof(struct ibio_dir);
    res = ibio_alloc_dir(dir.size);
    resend = (uchar*)res + dir.size;

    /* FIXME: put strings in dir then copy to *res */

    *res = dir;

    gsassert(__FILE__, __LINE__, bufsize == 0, "Didn't interpret %x octets of data", bufsize);

    return res;
}

void *ibio_dir_nursury;

struct gs_block_class ibio_dir;

struct ibio_dir_segment {
    struct gs_blockdesc desc; /* class = ibio_dir */
};

static void ibio_alloc_new_dir_block(void);

static
struct ibio_dir *
ibio_alloc_dir(ulong size)
{
    struct ibio_dir_segment *nursury_seg;
    struct ibio_dir *pres, *pnext;
    if (!ibio_dir_nursury)
        ibio_alloc_new_dir_block();

    gsassert(__FILE__, __LINE__, size >= sizeof(struct ibio_dir), "size smaller than reasonable for ibio_dir (%x); should exceed %x", size, sizeof(struct ibio_dir));
    gsassert(__FILE__, __LINE__, size < UXIO_IO_BUFFER_SIZE, "size supsiciously large: %x", size);

    nursury_seg = (struct ibio_dir_segment *)BLOCK_CONTAINING(ibio_dir_nursury);
    pres = (struct ibio_dir *)ibio_dir_nursury;
    pnext = pres + 1;
    if ((uchar*)pnext >= (uchar*)END_OF_BLOCK(nursury_seg))
        ibio_alloc_new_dir_block();
    else
        ibio_dir_nursury = pnext;

    return pres;
}

static
void
ibio_alloc_new_dir_block()
{
    struct ibio_dir_segment *nursury_seg;
    nursury_seg = gs_sys_seg_alloc(&ibio_dir);
    ibio_dir_nursury = (void*)((uchar*)nursury_seg + sizeof(*nursury_seg));
    gsassert(__FILE__, __LINE__, !((uintptr)ibio_dir_nursury % sizeof(gsvalue)), "ibio_dir_nursury not gsvalue-aligned; check sizeof(struct ibio_dir_nursury");
}
