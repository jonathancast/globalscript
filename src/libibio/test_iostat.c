#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "iosysconstants.h"
#include "iofile.h"
#include "iostat.h"
#include "iomacros.h"

#include "test_tests.h"

static void TEST_IOSTAT_DIR(void);
static void TEST_IOSTAT(void);

static struct uxio_ichannel *fixture_sample_chan_with_file_entry(void);
static struct uxio_ichannel *fixture_sample_chan_with_dir_entry(void);

void test_iostat()
{
    TEST_IOSTAT();
    TEST_IOSTAT_DIR();
}

static
void
TEST_IOSTAT()
{
    struct ibio_dir *pdir;
    struct uxio_ichannel *chan;

    fprint(2, "%s\n", "----\t" __FILE__ "\t TEST_IOSTAT\t----");

    chan = fixture_sample_chan_with_file_entry();
    pdir = ibio_parse_stat(chan);

    gsassert(__FILE__, __LINE__, !!pdir, "%s:%d: ibio_parse_stat returned null");
    gsassert_ulong_eq(__FILE__, __LINE__, pdir->size, sizeof(*pdir),
        "ibio_parse_stat()->size"
    );
    gsdeny(__FILE__, __LINE__, pdir->d.mode & DMDIR, "ibio_parse_stat returned directory, but was not a directory");
}

static
void
TEST_IOSTAT_DIR()
{
    struct ibio_dir *pdir;
    struct uxio_ichannel *chan;

    fprint(2, "%s\n", "----\t" __FILE__ "\t TEST_IOSTAT_DIR\t----");

    chan = fixture_sample_chan_with_dir_entry();
    pdir = ibio_parse_stat(chan);

    gsassert(__FILE__, __LINE__, !!pdir, "ibio_parse_stat returned null");
    gsassert(__FILE__, __LINE__, pdir->d.mode & DMDIR, "ibio_parse_stat returned not directory, but was a directory");
}

static
struct uxio_ichannel *
fixture_sample_chan_with_file_entry()
{
    struct uxio_ichannel *res;
    void *psize, *p;
    long size;

    res = ibio_get_channel_for_external_io(-1, ibio_iostat);

    size = 0;
    psize = uxio_save_space(res, 2);

    /* type */
    p = uxio_save_space(res, 2), size += 2;
    /* dev */
    p = uxio_save_space(res, 4), size += 4;
    /* qid.type */
    p = uxio_save_space(res, 1), size += 1;
    /* qid.vers */
    p = uxio_save_space(res, 4), size += 4;
    /* qid.path */
    p = uxio_save_space(res, 8), size += 8;
    /* mode */
    p = uxio_save_space(res, 4), size += 4;
    PUT_LITTLE_ENDIAN_U32INT(p, 0664);

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);

    return res;
}

static
struct uxio_ichannel *
fixture_sample_chan_with_dir_entry()
{
    struct uxio_ichannel *res;
    void *psize, *p;
    long size;

    res = ibio_get_channel_for_external_io(-1, ibio_iostat);

    size = 0;
    psize = uxio_save_space(res, 2);

    /* type */
    p = uxio_save_space(res, 2), size += 2;
    /* dev */
    p = uxio_save_space(res, 4), size += 4;
    /* qid.type */
    p = uxio_save_space(res, 1), size += 1;
    /* qid.vers */
    p = uxio_save_space(res, 4), size += 4;
    /* qid.path */
    p = uxio_save_space(res, 8), size += 8;
    /* mode */
    p = uxio_save_space(res, 4), size += 4;
    PUT_LITTLE_ENDIAN_U32INT(p, DMDIR | 0664);

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);

    return res;
}
