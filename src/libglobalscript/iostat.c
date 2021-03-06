#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "iosysconstants.h"
#include "iofile.h"
#include "iostat.h"
#include "iomacros.h"

struct gsbio_dir *
gsbio_stat(char *filename)
{
    struct uxio_ichannel *chan = gsbio_sys_stat(filename);
    if (!chan) return 0;
    return gsbio_consume_stat(chan);
}

struct gsbio_dir *
gsbio_read_stat(struct uxio_dir_ichannel *chan)
{
    long n;

    if ((n = gsbio_sys_read_stat(chan)) < 0)
        gsfatal("gsbio_sys_read_stat failed on channel %d: %r", chan->udir->fd);
    else if (n == 0)
        return 0;

    return gsbio_sys_parse_stat(chan);
}

#define NAMEMAX 0x100

struct gsbio_dir *
gsbio_consume_stat(struct uxio_ichannel *ip)
{
    void *buf;
    long n;
    u16int bufsize;

    /* size */
    if ((n = uxio_consume_space(ip, &buf, 2)) < 0) {
        werrstr("uxio_consume_space failed on channel %d: %r", ip->fd);
        return 0;
    } else if (n < 2) {
        werrstr(UNIMPL("Couldn't get size field; got %x octets"), n);
        return 0;
    }
    bufsize = GET_LITTLE_ENDIAN_U16INT(buf);
    gsassert(__FILE__, __LINE__, bufsize == uxio_channel_size_of_available_data(ip), "Reported size %x different than available data %x", bufsize, uxio_channel_size_of_available_data(ip));

    if ((n = uxio_consume_space(ip, &buf, bufsize)) < 0) {
        werrstr("uxio_consume_space failed on channel %d: %r", ip->fd);
        return 0;
    } else if (n < bufsize) {
        werrstr(UNIMPL("Couldn't get entire buffer; got %x octets"), n);
        return 0;
    }

    return gsbio_parse_stat(bufsize, buf);
}

static struct gs_sys_global_block_suballoc_info gsbio_dir_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "IBIO directory structures",
    },
};

struct gsbio_dir *
gsbio_parse_stat(u16int bufsize, void *start)
{
    char *buf, *bufend;
    struct gsbio_dir dir, *res;
    void *resend;
    long namesize;
    char name[NAMEMAX];

    buf = start;
    bufend = buf + bufsize;

    memset(&dir, 0, sizeof(dir));

    /* type, dev, qid.type, qid.vers, qid.path */
    buf += 2 + 4 + 1 + 4 + 8;

    /* mode */
    dir.d.mode = GET_LITTLE_ENDIAN_U32INT(buf);
    buf += 4;

    /* padding for atime */
    buf += 4;

    /* mtime */
    dir.d.mtime = GET_LITTLE_ENDIAN_U32INT(buf);
    buf += 4;

    /* padding for length */
    buf += 8;

    /* name */
    namesize = GET_LITTLE_ENDIAN_U16INT(buf);
    if (namesize >= NAMEMAX) {
        werrstr(UNIMPL("Name too long: %x octets but we only allow %x octets"), namesize, NAMEMAX);
        return 0;
    }
    buf += 2;
    memcpy(name, buf, namesize);
    name[namesize] = 0;
    buf += namesize;

    dir.size = sizeof(struct gsbio_dir) + namesize + 1;
    res = gs_sys_global_block_suballoc(&gsbio_dir_info, dir.size);
    resend = (uchar*)res + sizeof(struct gsbio_dir);

    memcpy(resend, name, namesize + 1);
    dir.d.name = resend;
    resend = (uchar*)res + namesize + 1;

    *res = dir;

    if (buf < bufend) {
        werrstr("Didn't interpret %x octets of data", bufend - buf);
        return 0;
    }

    return res;
}
