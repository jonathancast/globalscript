#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libtest.h>
#include <libglobalscript.h>

#include "iosysconstants.h"
#include "../iofile.h"
#include "../iomacros.h"
#include "iodir.h"

#include "test_systemtests.h"

static void TEST_UNIX_FILL_STAT(void);
static void TEST_UNIX_FILL_STAT_DIR(void);

void
test_iodir()
{
    RUNTESTS(TEST_UNIX_FILL_STAT);
    RUNTESTS(TEST_UNIX_FILL_STAT_DIR);
}

static struct uxio_ichannel *fixture_unix_stat_buffer_for_file(void);

static
void
TEST_UNIX_FILL_STAT()
{
    struct uxio_ichannel *chan;
    ulong size_of_data_stored;
    uchar buf[32];
    char str[32];
    u32int mode;
    long n, s;

    chan = fixture_unix_stat_buffer_for_file();

    size_of_data_stored = uxio_channel_size_of_available_data(chan);

    ok_ulong_ge(__FILE__, __LINE__, size_of_data_stored, 2, "Didn't read in a size");

    n = uxio_consume_space(chan, &buf, 2);
    ok_ulong_eq(__FILE__, __LINE__, n, 2, "Didn't read in a size");
    ok_ulong_eq(__FILE__, __LINE__, GET_LITTLE_ENDIAN_U16INT(buf), size_of_data_stored - 2, "Read in the wrong size");

    n = uxio_consume_space(chan, &buf, 2+4+1+4+8);
    ok_ulong_eq(__FILE__, __LINE__, n, 2+4+1+4+8, "Didn't read in the stuff before mode");

    n = uxio_consume_space(chan, &buf, 4);
    ok_ulong_eq(__FILE__, __LINE__, n, 4, "Didn't read in a mode");
    mode = GET_LITTLE_ENDIAN_U32INT(buf);
    not_ok(__FILE__, __LINE__, mode & DMDIR, "Indicated file was a directory in stored data");

    n = uxio_consume_space(chan, &buf, 4 + 4 + 8); /* atime + mtime + length */
    ok_ulong_eq(__FILE__, __LINE__, n, 4 + 4 + 8, "Didn't read in padding between the mode and the name");

    n = uxio_consume_space(chan, &buf, 2); /* name size */
    ok_ulong_eq(__FILE__, __LINE__, n, 2, "Didn't read in a size for the name");
    s = GET_LITTLE_ENDIAN_U16INT(buf);
    ok_ulong_eq(__FILE__, __LINE__, s, strlen("foo.txt"), "Size wasn't correct for name");
    n = uxio_consume_space(chan, &buf, s);
    ok_ulong_eq(__FILE__, __LINE__, n, s, "Couldn't read in entire name");
    memcpy(str, buf, s);
    str[s] = 0;
    ok_cstring_eq(__FILE__, __LINE__, str, "foo.txt", "Wrong name");

    ok_ulong_eq(__FILE__, __LINE__, uxio_channel_size_of_available_data(chan), 0, "Didn't test entire channel as stored");
}

static struct uxio_ichannel *fixture_unix_stat_buffer_for_dir(void);

static
void
TEST_UNIX_FILL_STAT_DIR()
{
    struct uxio_ichannel *chan;
    ulong size_of_data_stored;
    uchar buf[32];
    u32int mode;
    long n;

    chan = fixture_unix_stat_buffer_for_dir();

    size_of_data_stored = uxio_channel_size_of_available_data(chan);

    ok_ulong_ge(__FILE__, __LINE__, size_of_data_stored, 2, "Didn't read in a size");

    n = uxio_consume_space(chan, &buf, 2);
    ok_ulong_eq(__FILE__, __LINE__, n, 2, "Didn't read in a size");

    n = uxio_consume_space(chan, &buf, 2+4+1+4+8);
    ok_ulong_eq(__FILE__, __LINE__, n, 2+4+1+4+8, "Didn't read in the stuff before mode");

    n = uxio_consume_space(chan, &buf, 4);
    ok_ulong_eq(__FILE__, __LINE__, n, 4, "Didn't read in a mode");
    mode = GET_LITTLE_ENDIAN_U32INT(buf);

    ok(__FILE__, __LINE__, mode & DMDIR, "Didn't indicate file was a directory in stored data");
}

static
struct uxio_ichannel *
fixture_unix_stat_buffer_for_file()
{
    struct uxio_ichannel *chan;
    struct stat uxstat;

    memset(&uxstat, 0, sizeof(uxstat));

    chan = ibio_get_channel_for_external_io("", -1, ibio_iostat);
    gsbio_unix_fill_stat("./foo.txt", &uxstat, chan);

    return chan;
}

static
struct uxio_ichannel *
fixture_unix_stat_buffer_for_dir()
{
    struct uxio_ichannel *chan;
    struct stat uxstat;

    uxstat.st_mode = 0;
    uxstat.st_mode |= S_IFDIR;

    chan = ibio_get_channel_for_external_io("", -1, ibio_iostat);
    gsbio_unix_fill_stat("./foo", &uxstat, chan);

    return chan;
}

