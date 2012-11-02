#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "iosysconstants.h"
#include "iofile.h"
#include "iostat.h"
#include "iomacros.h"

static struct ibio_dir *ibio_alloc_dir(ulong size);

struct ibio_dir *
ibio_stat(char *filename)
{
    struct uxio_ichannel *chan = ibio_sys_stat(filename);
    return ibio_parse_stat(chan);
}

struct ibio_dir *
ibio_read_stat(struct uxio_dir_ichannel *chan)
{
    long n;

    if ((n = gsbio_sys_read_stat(chan)) < 0)
        gsfatal("gsbio_sys_read_stat failed on channel %d: %r", chan->udir->fd);
    else if (n == 0)
        return 0;

    return gsbio_sys_parse_stat(chan);
}

#define NAMEMAX 0x100

struct ibio_dir *
ibio_parse_stat(struct uxio_ichannel *ip)
{
    struct ibio_dir dir, *res;
    void *resend;
    uchar buf[32];
    long n, namesize;
    u16int bufsize;
    char name[NAMEMAX];

    memset(&dir, 0, sizeof(dir));

    /* size */
    if ((n = uxio_consume_space(ip, &buf, 2)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 2)
        gsfatal("%s:%d: Couldn't get size field; got %x octets", __FILE__, __LINE__, n);
    bufsize = GET_LITTLE_ENDIAN_U16INT(buf);
    gsassert(__FILE__, __LINE__, bufsize == uxio_channel_size_of_available_data(ip), "Reported size %x different than available data %x", bufsize, uxio_channel_size_of_available_data(ip));

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

    /* padding for atime, mtime, and length */
    if ((n = uxio_consume_space(ip, &buf, 4 + 4 + 8)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 4 + 4 + 8)
        gsfatal("%s:%d: Couldn't get padding for atime, mtime, and length fields; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 4 + 4 + 8;

    /* name */
    if ((n = uxio_consume_space(ip, &buf, 2)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < 2)
        gsfatal("%s:%d: Couldn't get size of name field; got %x octets", __FILE__, __LINE__, n);
    bufsize -= 2;
    namesize = GET_LITTLE_ENDIAN_U16INT(buf);
    if (namesize >= NAMEMAX)
        gsfatal("%s:%d: Name too long %x octets but we only allow %x octets", __FILE__, __LINE__, namesize, NAMEMAX);
    if ((n = uxio_consume_space(ip, &name, namesize)) < 0)
        gsfatal("uxio_consume_space failed on channel %d: %r", ip->fd);
    else if (n < namesize)
        gsfatal("%s:%d: Couldn't get name field; got %x octets; needed %x", __FILE__, __LINE__, n, namesize);
    bufsize -= namesize;
    name[namesize] = 0;

    dir.size = sizeof(struct ibio_dir) + namesize + 1;
    res = ibio_alloc_dir(dir.size);
    resend = (uchar*)res + sizeof(struct ibio_dir);

    memcpy(resend, name, namesize + 1);
    dir.d.name = resend;
    resend = (uchar*)res + namesize + 1;

    *res = dir;

    gsassert(__FILE__, __LINE__, bufsize == 0, "Didn't interpret %x octets of data", bufsize);

    return res;
}

void *ibio_dir_nursury;

struct gs_block_class ibio_dir = {
    /* evaluator = */ gsnoeval,
    /* description = */ "IBIO directory structures",
};

struct ibio_dir_segment {
    struct gs_blockdesc desc; /* class = ibio_dir */
};

static void ibio_alloc_new_dir_block(void);

static
struct ibio_dir *
ibio_alloc_dir(ulong size)
{
    struct ibio_dir_segment *nursury_seg;
    void *pres, *pnext;
    if (!ibio_dir_nursury)
        ibio_alloc_new_dir_block();

    gsassert(__FILE__, __LINE__, size >= sizeof(struct ibio_dir), "size smaller than reasonable for ibio_dir (%x); should exceed %x", size, sizeof(struct ibio_dir));
    gsassert(__FILE__, __LINE__, size < UXIO_IO_BUFFER_SIZE, "size supsiciously large: %x", size);

    nursury_seg = (struct ibio_dir_segment *)BLOCK_CONTAINING(ibio_dir_nursury);
    pres = ibio_dir_nursury;
    pnext = (uchar*)pres + size;
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
