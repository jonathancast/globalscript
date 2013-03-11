#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>
#include <libglobalscript.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "iosysconstants.h"
#include "../iofile.h"
#include "../iostat.h"
#include "../iomacros.h"
#include "iodir.h"

struct uxio_ichannel *
gsbio_sys_stat(char *filename)
{
    struct uxio_ichannel *chan;
    struct stat uxstat;
    chan = gsbio_get_channel_for_external_io("", -1, gsbio_iostat);
    if (stat(filename, &uxstat) < 0) {
        werrstr("%s: unix stat failed: %r", filename);
        return 0;
    }
    gsbio_unix_fill_stat(filename, &uxstat, chan);
    return chan;
}

static struct gs_sys_global_block_suballoc_info uxio_dir_ichannel_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Unix directory ichannels",
    },
};

struct uxio_dir_ichannel *
gsbio_dir_iopen(char *filename, int omode)
{
    struct uxio_dir_ichannel *res;

    res = gs_sys_global_block_suballoc(&uxio_dir_ichannel_info, sizeof(*res));

    if (!(res->udir = gsbio_device_iopen(filename, omode))) return 0;

    if (!(res->p9dir = gsbio_get_channel_for_external_io(res->udir->filename, -1, gsbio_ioread)))
        return 0
    ;

    return res;
}

long
gsbio_sys_read_stat(struct uxio_dir_ichannel *chan)
{
    long n;
    void *newp9dirbuf, *newudirbuf;

    if (chan->udir->fd < 0)
        return 0
    ;

    if (!chan->p9dir->filename || !*chan->p9dir->filename)
        return 0
    ;

    if (!uxio_channel_size_of_available_data(chan->udir)) {
        n = gsbio_unix_read_directory(chan->udir->fd, chan->udir->data_end, UXIO_END_OF_IO_BUFFER(chan->udir->buf_beg), &chan->udir->offset);
        if (n <= 0)
            return n
        ;
        chan->udir->data_end = (uchar*)chan->udir->data_end + n;
    }

    gsbio_unix_parse_directory(chan->p9dir->filename, chan->p9dir->data_end, UXIO_END_OF_IO_BUFFER(chan->p9dir->buf_beg), &newp9dirbuf, chan->udir->data_beg, chan->udir->data_end, &newudirbuf);
    if (!newp9dirbuf)
        return -1
    ;
    chan->p9dir->data_end = newp9dirbuf;
    chan->udir->data_beg = newudirbuf;
    if (chan->udir->data_end == chan->udir->data_beg)
        chan->udir->data_beg = chan->udir->data_end = chan->udir->buf_beg
    ;
    return uxio_channel_size_of_available_data(chan->p9dir);
}

long
gsbio_unix_read_directory(int fd, void *buf, void *end, vlong *poffset)
{
    /*
        NB: Apparently this has to be done in a Linux-specific way;
        this is cribbed from p9p's §fn{src/lib9/dirread.c:^/mygetdents/} (Linux version)

        Obviously, if you want to port this to other Unices you'll need to add §ccode{#ifdef}s and other implementations here
    */
    struct stat st;
    long sz;

    if (fstat(fd, &st) < 0)
        return -1
    ;

    if (st.st_blksize < 8192)
        st.st_blksize = 8192
    ;

    sz = (uchar*)end - (uchar*)buf;
    if (st.st_blksize < sz)
        sz = st.st_blksize
    ;

    return getdirentries(fd, buf, sz, poffset);
}

static void *gsbio_unix_dir_from_sys_stat(void *, void *, char *, struct stat *stat);

void
gsbio_unix_parse_directory(char *filename, void *p9buf, void *p9bufend, void **newp9buf, void *ubuf, void *ubufend, void **newubuf)
{
    struct dirent *dir;
    char *nm;
    struct stat st;

    while ((uchar*)ubuf < (uchar*)ubufend) {
        dir = ubuf;

        if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, "..")) {
            ubuf = (uchar*)ubuf + dir->d_reclen;
            continue;
        }
        nm = gs_sys_global_block_suballoc(&uxio_filename_info, strlen(filename) + strlen(dir->d_name) + 2);
        sprint(nm, "%s/%s", filename, dir->d_name);

        if (stat(nm, &st) < 0) {
            werrstr("%s: stat failed: %r", nm);
            *newp9buf = 0;
            return;
        }

        *newp9buf = gsbio_unix_dir_from_sys_stat(p9buf, p9bufend, nm, &st);
        *newubuf = (uchar*)ubuf + dir->d_reclen;
        return;
    }
    *newp9buf = p9buf;
    *newubuf = ubuf;
}

struct gsbio_dir *
gsbio_sys_parse_stat(struct uxio_dir_ichannel *chan)
{
    return gsbio_consume_stat(chan->p9dir);
}

void
gsbio_unix_fill_stat(char *filename, struct stat *puxstat, struct uxio_ichannel *chan)
{
    chan->data_end = gsbio_unix_dir_from_sys_stat(chan->data_end, UXIO_END_OF_IO_BUFFER(chan->buf_beg), filename, puxstat);
}

void *
gsbio_unix_dir_from_sys_stat(void *buf, void *bufend, char *filename, struct stat *puxstat)
{
    u64int mode;
    void *psize, *pdir;
    char *basename, *endname;
    char *str;
    int i;

    endname = filename;
    while (*endname) endname++;
    /* Strip trailing / */
    if (endname - 1 > filename && endname[-1] == '/')
        *--endname = 0
    ;
    basename = endname;
    while (basename > filename && basename[-1] != '/') basename--;

    psize = buf;
    buf = (uchar*)buf + 2;
    pdir = buf;

    /* type */
    PUT_LITTLE_ENDIAN_U16INT(buf, 'M');
    buf = (uchar*)buf + 2;
    /* dev */
    buf = (uchar*)buf + 4;
    /* qid.type */
    buf = (uchar*)buf + 1;
    /* qid.vers */
    buf = (uchar*)buf + 4;
    /* qid.path */
    buf = (uchar*)buf + 8;

    /* mode */
    mode = 0;
    if (S_ISDIR(puxstat->st_mode)) mode |= DMDIR;
    PUT_LITTLE_ENDIAN_U32INT(buf, mode);
    buf = (uchar*)buf + 4;

    /* Padding over atime, mtime, and length */
    buf = (uchar*)buf + 4 + 4 + 8;

    /* name[s] */
    PUT_LITTLE_ENDIAN_U16INT(buf, endname - basename);
    buf = (uchar*)buf + 2;
    str = buf;
    for (i = 0; i < endname - basename; i++)
        str[i] = basename[i]
    ;
    buf = (uchar*)buf + (endname - basename);

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)((uchar*)buf - (uchar*)pdir));
    return buf;
}

