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
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s: unix stat failed: %r", filename);
    gsbio_unix_fill_stat(filename, &uxstat, chan);
    return chan;
}

static struct gs_block_class uxio_dir_ichannel_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Unix directory ichannels",
};
static void *uxio_dir_ichannel_nursury;

struct uxio_dir_ichannel *
gsbio_dir_iopen(char *filename, int omode)
{
    struct uxio_dir_ichannel *res;

    res = gs_sys_seg_suballoc(&uxio_dir_ichannel_desc, &uxio_dir_ichannel_nursury, sizeof(*res), sizeof(res->udir));

    if (!(res->udir = gsbio_device_iopen(filename, omode)))
        return 0;

    if (!(res->p9dir = gsbio_get_channel_for_external_io(res->udir->filename, -1, gsbio_ioread)))
        return 0;

    return res;
}

long
gsbio_sys_read_stat(struct uxio_dir_ichannel *chan)
{
    struct dirent *dir;
    ulong sz;
    long n;
    struct stat st;
    char *nm;

    if (chan->udir->fd < 0)
        return 0;

    if (!chan->p9dir->filename || !*chan->p9dir->filename)
        return 0;

    /*
        NB: Apparently this has to be done in a Linux-specific way;
        this is cribbed from p9p's§fn<src/lib9/dirread.c:^/mygetdents/> (Linux version)
    */
    if (!uxio_channel_size_of_available_data(chan->udir)) {
        if (fstat(chan->udir->fd, &st) < 0)
            return -1;

        if (st.st_blksize < 8192)
            st.st_blksize = 8192;

        sz = (uchar*)UXIO_END_OF_IO_BUFFER(chan->udir->buf_beg) - (uchar*)chan->udir->data_end;
        if (st.st_blksize < sz)
            sz = st.st_blksize;

        if ((n = getdirentries(chan->udir->fd, (char*)chan->udir->data_end, sz, &chan->udir->offset)) <= 0)
            return n;

        chan->udir->data_end = (uchar*)chan->udir->data_end + n;
    }

    dir = (struct dirent*)chan->udir->data_beg;

    nm = gs_sys_seg_suballoc(
        &uxio_filename_class,
        &uxio_filename_nursury,
        strlen(chan->p9dir->filename) + strlen(dir->d_name) + 2,
        1
    );
    sprint(nm, "%s/%s", chan->p9dir->filename, dir->d_name);

    if (stat(nm, &st) < 0)
        gsfatal("%s: stat failed: %r", nm);

    gsbio_unix_fill_stat(nm, &st, chan->p9dir);
    chan->udir->data_beg = (uchar*)chan->udir->data_beg + dir->d_reclen;
    if (chan->udir->data_end == chan->udir->data_beg)
        chan->udir->data_beg = chan->udir->data_end = chan->udir->buf_beg
    ;

    return uxio_channel_size_of_available_data(chan->p9dir);
}

struct gsbio_dir *
gsbio_sys_parse_stat(struct uxio_dir_ichannel *chan)
{
    return gsbio_parse_stat(chan->p9dir);
}

void
gsbio_unix_fill_stat(char *filename, struct stat *puxstat, struct uxio_ichannel *chan)
{
    int size;
    u64int mode;
    void *psize, *p;
    char *basename, *endname;
    char *str;
    int i;

    endname = filename;
    while (*endname) endname++;
    basename = endname;
    while (basename > filename && basename[-1] != '/') basename--;

    size = 0;
    psize = uxio_save_space(chan, 2);

    /* type */
    p = uxio_save_space(chan, 2), size += 2;
    PUT_LITTLE_ENDIAN_U16INT(p, 'M');
    /* dev */
    p = uxio_save_space(chan, 4), size += 4;
    /* qid.type */
    p = uxio_save_space(chan, 1), size += 1;
    /* qid.vers */
    p = uxio_save_space(chan, 4), size += 4;
    /* qid.path */
    p = uxio_save_space(chan, 8), size += 8;

    /* mode */
    p = uxio_save_space(chan, 4), size += 4;
    mode = 0;
    if (S_ISDIR(puxstat->st_mode)) mode |= DMDIR;
    PUT_LITTLE_ENDIAN_U32INT(p, mode);

    /* Padding over atime, mtime, and length */
    p = uxio_save_space(chan, 4 + 4 + 8), size += 4 + 4 + 8;

    /* name[s] */
    p = uxio_save_space(chan, 2), size += 2;
    PUT_LITTLE_ENDIAN_U16INT(p, endname - basename);
    p = uxio_save_space(chan, endname - basename), size += endname - basename;
    str = p;
    for (i = 0; i < endname - basename; i++)
        str[i] = basename[i]
    ;

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);
}

