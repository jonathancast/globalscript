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

static void ibio_unix_fill_stat(char *filename, struct stat *, struct uxio_channel *);

struct uxio_channel *
ibio_sys_stat(char *filename)
{
    struct uxio_channel *chan;
    struct stat uxstat;
    chan = ibio_get_channel_for_external_io(-1, ibio_iostat);
    if (stat(filename, &uxstat) < 0)
        gsfatal("%s: unix stat failed: %r", filename);
    ibio_unix_fill_stat(filename, &uxstat, chan);
    return chan;
}

static void ibio_unix_assert_stat_valid(struct uxio_channel *chan, char *filename);

static void
ibio_unix_fill_stat(char *filename, struct stat *puxstat, struct uxio_channel *chan)
{
    int size;
    void *psize, *ptype;

    size = 0;
    psize = uxio_save_space(chan, 2);

    ptype = uxio_save_space(chan, 2), size += 2;
    PUT_LITTLE_ENDIAN_U16INT(ptype, 0);

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);

    ibio_unix_assert_stat_valid(chan, filename);

    gsfatal("unix ibio_unix_fill_stat(%s) next", filename);
}

static
void
ibio_unix_assert_stat_valid(struct uxio_channel *chan, char *filename)
{
    ulong size_of_data_stored, size_in_chan, size_of_data_tested;
    size_of_data_stored = uxio_channel_size_of_available_data(chan);
    size_of_data_tested = 0;

    /* size */
    gsassert(
        size_of_data_stored >= size_of_data_tested + 2,
        "unix ibio_unix_fill_stat(%s): Didn't read in even a size; read in %x octets!",
        filename,
        size_of_data_stored
    );
    size_in_chan = (uint)GET_LITTLE_ENDIAN_U16INT((uchar*)chan->buf_beg + 0);
    gsassert(size_of_data_stored == size_in_chan + 2,
        "unix ibio_unix_fill_stat(%s): Size (%x) doesn't match size of available data (%lx)",
        filename,
        size_in_chan + 2,
        size_of_data_stored
    );
    size_of_data_tested += 2;

    /* type */
    gsassert(
        size_of_data_stored >= size_of_data_tested + 2,
        "unix ibio_unix_fill_stat(%s): Didn't read in a type; read in %x octets!",
        filename,
        size_of_data_stored
    );
    size_of_data_tested += 2;

    /* dev */
    gsassert(

    gsfatal("ibio_unix_assert_stat_valid next");
}
