/* §source.file Unix System Call-Level File I/O */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "../ibio.h"

static struct ibio_uxio *ibio_alloc_uxio(ulong, ibio_uxio_refill *);

/* §section Files */

static struct ibio_uxio *ibio_file_io_value;
static Lock ibio_file_io_lock;

static ibio_uxio_refill ibio_file_refill;

struct ibio_uxio *
ibio_file_io(void)
{
    lock(&ibio_file_io_lock);
    if (ibio_file_io_value) {
        unlock(&ibio_file_io_lock);
        return ibio_file_io_value;
    }

    ibio_file_io_value = ibio_alloc_uxio(sizeof(*ibio_file_io_value), ibio_file_refill);

    unlock(&ibio_file_io_lock);
    return ibio_file_io_value;
}

int
ibio_file_refill(struct ibio_uxio *uxio, int fd, void *buf, long n)
{
    return read(fd, buf, n);
}

/* §section Allocation */

static struct gs_block_class ibio_uxio_descr ={
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
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
    res = gs_sys_seg_suballoc(&ibio_uxio_descr, &ibio_uxio_nursury, sz, sizeof(void*));
    unlock(&ibio_uxio_lock);
    res->refill = refill;

    return res;
}
