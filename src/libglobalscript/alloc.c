/* §source.file{The Simple Segment Manager} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"

static Lock gs_allocator_lock;

#define GS_SYS_MAX_NUM_SEGMENTS 5

static int gs_sys_num_segments;
static struct gs_sys_segment {
    enum {
        gs_sys_segment_break,
        gs_sys_segment_free,
        gs_sys_segment_allocated,
    } type;
    void *base;
} gs_sys_segments[GS_SYS_MAX_NUM_SEGMENTS];

typedef uintptr gsumemorysize; /* TODO: Horrid hack */

void
gs_sys_memory_init(void)
{
    void *bottom_of_data;

    bottom_of_data = sbrk(0);
    if ((uintptr)bottom_of_data % BLOCK_SIZE) {
        bottom_of_data = (uchar*)bottom_of_data + BLOCK_SIZE - ((uintptr)bottom_of_data % BLOCK_SIZE);
        if (brk(bottom_of_data) < 0)
            gsfatal("brk failed! %r")
        ;
    }
    if ((uintptr)bottom_of_data >= GS_MAX_PTR)
        gsfatal("Out of memory in gs_sys_memory_init")
    ;

    gs_sys_num_segments = 1;
    gs_sys_segments[0].type = gs_sys_segment_break;
    gs_sys_segments[0].base = bottom_of_data;
}

#define GS_SYS_SEGMENT_SIZE(i) \
    ((i) < gs_sys_num_segments \
        ? (uchar*)gs_sys_segments[(i) + 1].base - (uchar*)gs_sys_segments[i].base \
        : (uchar*)GS_MAX_PTR - (uchar*)gs_sys_segments[i].base \
    )

#define GS_SYS_SEGMENT_IS(i, ty) \
    ((i) >= 0 && (i) < gs_sys_num_segments && gs_sys_segments[i].type == ty)

#define GS_SPLIT_SEGMENT(i, ty, sz) \
    do { \
        if (gs_sys_num_segments >= GS_SYS_MAX_NUM_SEGMENTS) \
            gswarning("%s:%d: Things are about to break b/c we're out of segments", __FILE__, __LINE__) \
        ; \
        for (j = gs_sys_num_segments; j > i; j--) \
            gs_sys_segments[j] = gs_sys_segments[j - 1] \
        ; \
        gs_sys_segments[i + 1].type = gs_sys_segments[i].type; \
        gs_sys_segments[i + 1].base = (uchar*)gs_sys_segments[i].base + sz; \
        gs_sys_segments[i].type = ty; \
        gs_sys_num_segments++; \
    } while (0)

#define GS_SYS_COALESCE_WITH_PREVIOUS_SEGMENT(i, nsegs) \
    do { \
        for (j = i; j + nsegs < gs_sys_num_segments; j++) \
            gs_sys_segments[j] = gs_sys_segments[j + nsegs] \
        ; \
        gs_sys_num_segments -= nsegs; \
    } while (0)

/* Needs to lock the program break */
void *
gs_sys_block_alloc(registered_block_class cl)
{
    int i, j;
    struct gs_blockdesc *pres;

    lock(&gs_allocator_lock);

    if (!gs_sys_num_segments)
        gs_sys_memory_init()
    ;

    for (i = 0; i < gs_sys_num_segments; i++) {
        switch (gs_sys_segments[i].type) {
            case gs_sys_segment_break: {
                gsumemorysize old_memory_size, break_size, new_free_segment_size;

                old_memory_size = (uchar*)gs_sys_segments[i].base - (uchar*)gs_sys_segments[0].base;
                break_size = (uchar*)GS_MAX_PTR - (uchar*)gs_sys_segments[i].base;

                new_free_segment_size = old_memory_size;
                if (new_free_segment_size == 0) new_free_segment_size = BLOCK_SIZE;
                if (new_free_segment_size >= break_size) new_free_segment_size = break_size;
                if (brk((uchar*)gs_sys_segments[i].base + new_free_segment_size) < 0)
                    gswarning("%s:%d: Could not extend data segment: brk failed (allocated %p to %p, allocating %p)! %r", __FILE__, __LINE__, gs_sys_segments[0].base, gs_sys_segments[i].base, new_free_segment_size)
                ;
                if (new_free_segment_size < break_size) {
                    GS_SPLIT_SEGMENT(i, gs_sys_segment_free, new_free_segment_size);
                } else {
                    gswarning("%s:%d: Extended program break to maximum location", __FILE__, __LINE__);
                    gs_sys_segments[i].type = gs_sys_segment_free;
                }
            }
            /* Deliberately falling through; §ccode{i} needs to point at the §emph{free} segment we just created now */
            case gs_sys_segment_free:
                pres = (struct gs_blockdesc *)gs_sys_segments[i].base;
                if (GS_SYS_SEGMENT_SIZE(i) > BLOCK_SIZE) {
                    if (GS_SYS_SEGMENT_IS(i - 1, gs_sys_segment_allocated)) {
                        gs_sys_segments[i].base = (uchar*)gs_sys_segments[i].base + BLOCK_SIZE;
                    } else {
                        GS_SPLIT_SEGMENT(i, gs_sys_segment_allocated, BLOCK_SIZE);
                    }
                } else {
                    if (
                        GS_SYS_SEGMENT_IS(i - 1, gs_sys_segment_allocated)
                        && GS_SYS_SEGMENT_IS(i + 1, gs_sys_segment_allocated)
                    ) {
                        GS_SYS_COALESCE_WITH_PREVIOUS_SEGMENT(i, 2);
                    } else if (GS_SYS_SEGMENT_IS(i - 1, gs_sys_segment_allocated)) {
                        GS_SYS_COALESCE_WITH_PREVIOUS_SEGMENT(i, 1);
                    } else if (GS_SYS_SEGMENT_IS(i + 1, gs_sys_segment_allocated)) {
                        gs_sys_segments[i].type = gs_sys_segment_allocated;
                        GS_SYS_COALESCE_WITH_PREVIOUS_SEGMENT(i + 1, 1);
                    } else {
                        /* §paragraph{Just mark this segment allocated} */
                        gs_sys_segments[i].type = gs_sys_segment_allocated;
                    }
                }
                goto have_pres;
        }
    }
    gswarning("%s:%d: About to stop because we're out of memory", __FILE__, __LINE__);
    pres = 0;

have_pres:

    pres->class = cl;

    unlock(&gs_allocator_lock);

    return (void*)pres;
}

#define ALIGN_TO(p, align) \
    do { \
        if ((uintptr)(p) % (align)) \
            (p) = (uchar*)(p) + (align) - ((uintptr)(p) % (align)) \
        ; \
    } while (0)

void *
gs_sys_block_suballoc(registered_block_class cl, void **pnursury, ulong sz, ulong align)
{
    struct gs_blockdesc *nursury_block;
    void *res;

    if (!*pnursury) {
        nursury_block = gs_sys_block_alloc(cl);
        *pnursury = START_OF_BLOCK(nursury_block);
        ALIGN_TO(*pnursury, align);
    } else {
        nursury_block = BLOCK_CONTAINING(*pnursury);
    }

    if ((uchar*)*pnursury + sz > (uchar*)END_OF_BLOCK(nursury_block)) {
        nursury_block = gs_sys_block_alloc(cl);
        *pnursury = START_OF_BLOCK(nursury_block);
        ALIGN_TO(*pnursury, align);
    }

    res = *pnursury;
    *pnursury = (uchar*)res + sz;
    ALIGN_TO(*pnursury, align);
    if ((uchar*)*pnursury >= (uchar*)END_OF_BLOCK(nursury_block))
        *pnursury = 0
    ;

    return res;
}

