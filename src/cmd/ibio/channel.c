/* §source.file Channels (input- or output-end-independent code) */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

#define IBIO_CHANNEL_SEGMENT_SIZE (0x400 * sizeof(gsvalue))

/* §section Allocation */

static gstypecode ibio_channel_eval(gsvalue);
static gsvalue ibio_channel_remove_indir(gsvalue);

static struct gs_block_class ibio_channel_segment_descr = {
    /* evaluator = */ ibio_channel_eval,
    /* indirection_dereferencer = */ ibio_channel_remove_indir,
    /* description = */ "IBIO Channels",
};
static void *ibio_channel_segment_nursury;
static Lock ibio_channel_segment_lock;

struct ibio_channel_segment *
ibio_alloc_channel_segment()
{
    struct gs_blockdesc *nursury_block;
    struct ibio_channel_segment *res;

    lock(&ibio_channel_segment_lock);
    if (ibio_channel_segment_nursury) {
        res = ibio_channel_segment_nursury;
        nursury_block = BLOCK_CONTAINING(res);
        if ((char*)ibio_channel_segment_nursury >= (char*)END_OF_BLOCK(nursury_block) - IBIO_CHANNEL_SEGMENT_SIZE)
            ibio_channel_segment_nursury = 0
        ; else
            ibio_channel_segment_nursury = (char*)res + IBIO_CHANNEL_SEGMENT_SIZE
        ;
    } else {
        nursury_block = gs_sys_seg_alloc(&ibio_channel_segment_descr);
        res = START_OF_BLOCK(nursury_block);
        ibio_channel_segment_nursury = (char*)nursury_block + IBIO_CHANNEL_SEGMENT_SIZE;
    }
    unlock(&ibio_channel_segment_lock);

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);

    res->next = 0;
    res->extent = res->items;

    return res;
}

gsvalue *
ibio_channel_segment_limit(struct ibio_channel_segment *seg)
{
    char *block;

    block = (char*)seg - (uintptr)seg % IBIO_CHANNEL_SEGMENT_SIZE;
    return (gsvalue*)block + IBIO_CHANNEL_SEGMENT_SIZE;
}

/* §section Evaluation */

static
gstypecode
ibio_channel_eval(gsvalue v)
{
    struct ibio_channel_segment *seg;

    seg = ibio_channel_segment_containing((gsvalue *)v);

    /* Note: §ccode{v} is a §gs{iptr}; user has to de-reference it in GS */
    if (v >= (uintptr)seg->items && v < (uintptr)seg->extent) {
        unlock(&seg->lock);
        return gstywhnf;
    }

    if (v == (uintptr)seg->extent)
        if (seg->next) {
            /* Special representation for EOF */
            unlock(&seg->lock);
            return gstywhnf;
        } else {
            /* This can happen with the result of §gs{read}, if the evaluation happens before we've decided whether it's a symbol or an EOF */
            unlock(&seg->lock);
            return gstyblocked;
        }
    ;

    unlock(&seg->lock);
    werrstr("invalid gsvalue %p", v);
    return gstyenosys;
}

static
gsvalue
ibio_channel_remove_indir(gsvalue v)
{
    return v;
}

struct ibio_channel_segment *
ibio_channel_segment_containing(gsvalue *p)
{
    struct ibio_channel_segment *res;
    void *block;

    block = (void*)((uchar*)p - ((uintptr)p % IBIO_CHANNEL_SEGMENT_SIZE));
    res = (uintptr)block % BLOCK_SIZE
        ? (struct ibio_channel_segment *)block
        : (struct ibio_channel_segment *)START_OF_BLOCK((struct gs_blockdesc *)block)
    ;

    lock(&res->lock);
    return res;
}
