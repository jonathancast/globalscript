/* §source.file Unix System Call-Level File I/O */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "../ibio.h"

static struct ibio_uxio *ibio_alloc_uxio(ulong, ibio_uxio_refill *);

/* §section Files */

static struct ibio_uxio *ibio_file_uxio_value;
static Lock ibio_file_uxio_lock;

static ibio_uxio_refill ibio_file_refill;

struct ibio_uxio *
ibio_file_uxio()
{
    lock(&ibio_file_uxio_lock);
    if (ibio_file_uxio_value) {
        unlock(&ibio_file_uxio_lock);
        return ibio_file_uxio_value;
    }

    ibio_file_uxio_value = ibio_alloc_uxio(sizeof(*ibio_file_uxio_value), ibio_file_refill);

    unlock(&ibio_file_uxio_lock);
    return ibio_file_uxio_value;
}

long
ibio_file_refill(struct ibio_uxio *uxio, int fd, void *buf, long n)
{
    return read(fd, buf, n);
}

/* §section Directories */

static ibio_uxio_refill ibio_dir_refill;

#define DIR_SEGMENT_SIZE 0x4000

struct ibio_dir_uxio {
    struct ibio_uxio uxio;
    char *dirname;
    void *bufbeg, *bufend;
    vlong offset;
};

struct ibio_uxio *
ibio_dir_uxio(char *dirname)
{
    struct ibio_dir_uxio *res;

    res = (struct ibio_dir_uxio *)ibio_alloc_uxio(DIR_SEGMENT_SIZE, ibio_dir_refill);
    res->dirname = dirname;
    res->bufend = res->bufbeg = (uchar*)res + sizeof(*res);
    res->offset = 0;

    return (struct ibio_uxio *)res;
}

long
ibio_dir_refill(struct ibio_uxio *uxio, int fd, void *buf, long n)
{
    struct ibio_dir_uxio *dirio;
    void *newbuf, *newdirbuf;
    long res;

    dirio = (struct ibio_dir_uxio *)uxio;

    if (dirio->bufbeg == dirio->bufend) {
        dirio->bufbeg = (uchar*)dirio + sizeof(*dirio);
        res = gsbio_unix_read_directory(fd, dirio->bufbeg, (uchar*)dirio + DIR_SEGMENT_SIZE, &dirio->offset);
        if (res <= 0)
            return res
        ;
        dirio->bufend = (uchar*)dirio->bufbeg + res;
    }

    gsbio_unix_parse_directory(dirio->dirname, buf, (char*)buf + n, &newbuf, dirio->bufbeg, dirio->bufend, &newdirbuf);
    if (newbuf) {
        dirio->bufbeg = newdirbuf;
        return (uchar*)newbuf - (uchar*)buf;
    } else {
        return -1;
    }
}

/* §section Allocation */

static struct gs_block_class ibio_uxio_descr ={
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Unix system call-level file I/O descriptors",
};
static void *ibio_uxio_nursury;
static Lock ibio_uxio_lock;

static
struct ibio_uxio *
ibio_alloc_uxio(ulong sz, ibio_uxio_refill *refill)
{
    struct ibio_uxio *res;

    lock(&ibio_uxio_lock);
    res = gs_sys_block_suballoc(&ibio_uxio_descr, &ibio_uxio_nursury, sz, sizeof(void*));
    unlock(&ibio_uxio_lock);
    res->refill = refill;

    return res;
}
