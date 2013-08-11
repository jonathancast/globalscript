#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

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
    RUNTESTS(TEST_IOSTAT);
    RUNTESTS(TEST_IOSTAT_DIR);
}

#define TEST_FIXTURE_MTIME 1987654321

static
void
TEST_IOSTAT()
{
    struct gsbio_dir *pdir;
    struct uxio_ichannel *chan;

    chan = fixture_sample_chan_with_file_entry();
    pdir = gsbio_consume_stat(chan);

    ok(__FILE__, __LINE__, !!pdir, "%s:%d: gsbio_parse_stat returned null");
    ok_ulong_eq(__FILE__, __LINE__, pdir->size, sizeof(*pdir) + sizeof("foo.txt"),
        "gsbio_parse_stat()->size"
    );
    not_ok(__FILE__, __LINE__, pdir->d.mode & DMDIR, "gsbio_parse_stat returned directory, but was not a directory");
    ok_ulong_eq(__FILE__, __LINE__, pdir->d.mtime, TEST_FIXTURE_MTIME, "gsbio_parse_stat returned incorrect mtime");
    ok_cstring_eq(__FILE__, __LINE__, pdir->d.name, "foo.txt", "gsbio_parse_stat returned incorrect name");
}

static
void
TEST_IOSTAT_DIR()
{
    struct gsbio_dir *pdir;
    struct uxio_ichannel *chan;

    chan = fixture_sample_chan_with_dir_entry();
    pdir = gsbio_consume_stat(chan);

    ok(__FILE__, __LINE__, !!pdir, "gsbio_parse_stat returned null");
    ok(__FILE__, __LINE__, pdir->d.mode & DMDIR, "gsbio_parse_stat returned not directory, but was a directory");
    ok_cstring_eq(__FILE__, __LINE__, pdir->d.name, "foo", "gsbio_parse_stat returned incorrect name");
}

static
struct uxio_ichannel *
fixture_sample_chan_with_file_entry()
{
    struct uxio_ichannel *res;
    void *psize, *p;
    long size;

    res = gsbio_get_channel_for_external_io(0, -1, gsbio_iostat);

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

    /* padding for atime */
    p = uxio_save_space(res, 4), size += 4;

    /* padding for mtime */
    p = uxio_save_space(res, 4), size += 4;
    PUT_LITTLE_ENDIAN_U32INT(p, TEST_FIXTURE_MTIME);

    /* padding for length */
    p = uxio_save_space(res, 8), size += 8;

    /* name */
    p = uxio_save_space(res, 2), size += 2;
    PUT_LITTLE_ENDIAN_U16INT(p, strlen("foo.txt"));
    p = uxio_save_space(res, strlen("foo.txt")), size += strlen("foo.txt");
    memcpy(p, "foo.txt", strlen("foo.txt"));

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

    res = gsbio_get_channel_for_external_io("", -1, gsbio_iostat);

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

    /* padding for atime, mtime, and length */
    p = uxio_save_space(res, 4 + 4 + 8), size += 4 + 4 + 8;

    /* name */
    p = uxio_save_space(res, 2), size += 2;
    PUT_LITTLE_ENDIAN_U16INT(p, strlen("foo"));
    p = uxio_save_space(res, strlen("foo")), size += strlen("foo");
    memcpy(p, "foo", strlen("foo"));

    PUT_LITTLE_ENDIAN_U16INT(psize, (u16int)size);

    return res;
}
