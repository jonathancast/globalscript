/* §source.file Channels (input- or output-end-independent code) */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

#define IBIO_CHANNEL_SEGMENT_SIZE (0x400 * sizeof(gsvalue))

/* §section Allocation */

static gstypecode ibio_channel_eval(gsvalue);
static gsvalue ibio_channel_remove_indir(gsvalue);
static gsvalue ibio_iptr_gc_trace(struct gsstringbuilder *err, gsvalue);

static struct gs_sys_aligned_block_suballoc_info ibio_channel_segment_info = {
    /* descr = */ {
        /* evaluator = */ ibio_channel_eval,
        /* indirection_dereferencer = */ ibio_channel_remove_indir,
        /* gc_trace = */ ibio_iptr_gc_trace,
        /* description = */ "IBIO Channels",
    },
    /* align = */ IBIO_CHANNEL_SEGMENT_SIZE,
};

struct ibio_channel_segment *
ibio_alloc_channel_segment()
{
    void *buf;
    struct ibio_channel_segment *res;

    gs_sys_aligned_block_suballoc(&ibio_channel_segment_info, &buf, 0);
    res = (struct ibio_channel_segment *)buf;

    memset(&res->lock, 0, sizeof(res->lock));
    lock(&res->lock);

    res->next = 0;
    res->extent = res->beginning = res->items;
    res->forward = 0;

    return res;
}

gsvalue *
ibio_channel_segment_limit(struct ibio_channel_segment *seg)
{
    /* Segments come in two varieties:
        §begin{itemize}
            §item The first segment in each block, which extends from §ccode{(char *)block + sizeof(struct gs_blockdesc)} to §ccode{(char*)block + IBIO_CHANNEL_SEGMENT_SIZE}.
            §item The $n$th segment in a block for $n > 0$, which start at §ccode{(char *)block + IBIO_CHANNEL_SEGMENT_SIZE * n}.
        §end{itemize}
        We want all of those segments to have the same size for GC purposes, so we assign them all the size §ccode{IBIO_CHANNEL_SEGMENT_SIZE - sizeof(struct gs_blockdesc)}.
        This wastes about one word per 255 blocks, which is pretty reasonable.
    */
    return (gsvalue *)((char*)seg + IBIO_CHANNEL_SEGMENT_SIZE - sizeof(struct gs_blockdesc));
}

/* §section Evaluation */

static
gstypecode
ibio_channel_eval(gsvalue v)
{
    struct ibio_channel_segment *seg;

    seg = ibio_channel_segment_containing((gsvalue *)v);

    /* Note: §ccode{v} is an §gs{iptr}; user has to de-reference it in GS */
    if (v >= (uintptr)seg->items && v < (uintptr)seg->extent) {
        unlock(&seg->lock);
        return gstywhnf;
    }

    if (v == (uintptr)seg->extent)
        if (seg->next) {
            /* Special representation for EOF */
            unlock(&seg->lock);
            return gstywhnf;
        } else if (seg->iport->error) {
            unlock(&seg->lock);
            return gstyindir;
        } else {
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
    struct ibio_channel_segment *seg;

    seg = ibio_channel_segment_containing((gsvalue *)v);

    if (v == (uintptr)seg->extent) {
        if (seg->next) {
            unlock(&seg->lock);
            return v;
        } else if (seg->iport->error) {
            gsvalue res;

            res = seg->iport->error;
            unlock(&seg->lock);
            return res;
        } else {
            unlock(&seg->lock);
            return v;
        }
    } else {
        unlock(&seg->lock);
        return v;
    }
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

/* §section Garbage-collection */

int
ibio_iptr_live(gsvalue *iptr)
{
    struct ibio_channel_segment *seg;

    seg = ibio_channel_segment_containing(iptr);
    unlock(&seg->lock);

    return (seg->forward && iptr >= seg->beginning) || !gs_sys_block_in_gc_from_space(seg);
}

struct ibio_channel_segment *
ibio_channel_segment_lookup_forward(struct ibio_channel_segment *seg)
{
    return seg->forward;
}

gsvalue *
ibio_iptr_lookup_forward(gsvalue *iptr)
{
    struct ibio_channel_segment *seg;

    seg = ibio_channel_segment_containing(iptr);
    unlock(&seg->lock);

    if (!seg->forward) return iptr;

    return seg->forward->items + (iptr - seg->items);
}

static int ibio_channel_segment_trace(struct gsstringbuilder *, struct ibio_channel_segment *, struct ibio_channel_segment **, int);

int
ibio_iptr_trace(struct gsstringbuilder *err, gsvalue **piptr)
{
    struct ibio_channel_segment *seg, *newseg;
    gsvalue *iptr, *newiptr, *tmpiptr;
    gsvalue gctemp;

    iptr = *piptr;

    seg = ibio_channel_segment_containing(iptr);
    unlock(&seg->lock);

    if (ibio_channel_segment_trace(err, seg, &newseg, 0) < 0) return -1;

    *piptr = newiptr = newseg->items + (iptr - seg->items);

    for (tmpiptr = newiptr; tmpiptr < newseg->beginning; tmpiptr++)
        if (GS_GC_TRACE(err, tmpiptr) < 0) return -1
    ;

    if (newiptr < newseg->beginning) newseg->beginning = newiptr;

    return 0;
}

int
ibio_channel_segment_trace(struct gsstringbuilder *err, struct ibio_channel_segment *seg, struct ibio_channel_segment **pnewseg, int copy_everything)
{
    struct ibio_channel_segment *newseg;
    gsvalue *tmpiptr;
    gsvalue gctemp;

    if (seg->forward) {
        *pnewseg = newseg = seg->forward;

        if (copy_everything) {
            gsstring_builder_print(err, UNIMPL("Copy rest of segment"));
            return -1;
        }

        return 0;
    } else {
        void *buf;

        gs_sys_aligned_block_suballoc(&ibio_channel_segment_info, &buf, 0);
        *pnewseg = newseg = (struct ibio_channel_segment *)buf;

        memcpy(buf, seg, (uchar*)ibio_channel_segment_limit(seg) - (uchar*)seg);
        memset(&newseg->lock, 0, sizeof(newseg->lock));
        newseg->extent = newseg->beginning = newseg->items + (seg->extent - seg->items);

        seg->forward = newseg;

        if (copy_everything) {
            for (tmpiptr = newseg->items; tmpiptr < newseg->beginning; tmpiptr++)
                if (GS_GC_TRACE(err, tmpiptr) < 0) return -1
            ;
        }

        if (newseg->next) {
            if (ibio_channel_segment_trace(err, newseg->next, &newseg->next, 1) < 0) return -1;
        }

        return 0;
    }
}

static
gsvalue
ibio_iptr_gc_trace(struct gsstringbuilder *err, gsvalue v)
{
    gsvalue *iptr;

    iptr = (gsvalue *)v;

    if (ibio_iptr_trace(err, &iptr) < 0) return 0;

    return (gsvalue)iptr;
}
