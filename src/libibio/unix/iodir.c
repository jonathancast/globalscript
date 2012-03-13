#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>
#include <libglobalscript.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libibio.h>
#include "iosysconstants.h"
#include "../iofile.h"
#include "../iostat.h"
#include "../iomacros.h"
#include "iodir.h"

struct uxio_ichannel *
ibio_sys_stat(char *filename)
{
    struct uxio_ichannel *chan;
    struct stat uxstat;
    chan = ibio_get_channel_for_external_io(-1, ibio_iostat);
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s: unix stat failed: %r", filename);
    ibio_unix_fill_stat(filename, &uxstat, chan);
    return chan;
}

void
ibio_unix_fill_stat(char *filename, struct stat *puxstat, struct uxio_ichannel *chan)
{
    int size;
    u64int mode;
    void *psize, *p;

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

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);
}

