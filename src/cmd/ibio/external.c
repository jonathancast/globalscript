/* §source.file External File I/O */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

static struct ibio_external_io *ibio_alloc_external_io(long, ibio_external_canread *, ibio_external_readsym *);

/* §section Runes */

static struct ibio_external_io *ibio_rune_io_value;
static Lock ibio_rune_io_lock;

static ibio_external_canread ibio_rune_canread;
static ibio_external_readsym ibio_rune_readsym;

struct ibio_external_io *
ibio_rune_io()
{
    lock(&ibio_rune_io_lock);
    if (ibio_rune_io_value) {
        unlock(&ibio_rune_io_lock);
        return ibio_rune_io_value;
    }

    ibio_rune_io_value = ibio_alloc_external_io(sizeof(*ibio_rune_io_value), ibio_rune_canread, ibio_rune_readsym);

    unlock(&ibio_rune_io_lock);
    return ibio_rune_io_value;
}

static
int
ibio_rune_canread(struct ibio_external_io *io, void *start, void *end)
{
    uchar fst;

    if ((uchar*)start >= (uchar*)end)
        return 0
    ;
    fst = *(uchar*)start;
    if (fst < 0x80)
        return (uchar*)start <= (uchar*)end - 1
    ; else if (fst < 0xc0)
        return (uchar*)start <= (uchar*)end - 2
    ; else if (fst < 0xe0)
        return (uchar*)start <= (uchar*)end - 3
    ; else
        return (uchar*)start <= (uchar*)end - 4
    ;
}

static
void *
ibio_rune_readsym(struct ibio_external_io *io, char *err, char *eerr, void *start, void *end, gsvalue *pres)
{
    return gschartorune((char*)start, pres, err, eerr);
}

/* §section Allocation */

static struct gs_block_class ibio_external_io_descr ={
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "External I/O descriptors",
};
static void *ibio_external_io_nursury;
static Lock ibio_external_io_lock;

static
struct ibio_external_io *
ibio_alloc_external_io(long sz, ibio_external_canread *canread, ibio_external_readsym *readsym)
{
    struct ibio_external_io *res;

    lock(&ibio_external_io_lock);
    res = gs_sys_seg_suballoc(&ibio_external_io_descr, &ibio_external_io_nursury, sz, sizeof(void*));
    unlock(&ibio_external_io_lock);
    res->canread = canread;
    res->readsym = readsym;

    return res;
}
