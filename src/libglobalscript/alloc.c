/* §source.file{The Simple Segment Manager} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsalloc.h"
#include "gsproc.h"

Lock gs_allocator_lock;

#define GS_SYS_MAX_NUM_SEGMENTS 5

static int gs_sys_num_segments;
static struct gs_sys_segment {
    enum {
        gs_sys_segment_break,
        gs_sys_segment_free,
        gs_sys_segment_allocated,
        gs_sys_segment_gc_from_space,
    } type;
    void *base;
} gs_sys_segments[GS_SYS_MAX_NUM_SEGMENTS];
static int gs_sys_gc_num_procs_waiting;
int gs_sys_num_procs = 1;
static int gs_sys_in_gc;

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

#define GS_SYS_SEGMENT_TOP(i) ((i) < gs_sys_num_segments ? gs_sys_segments[(i) + 1].base : (void*)GS_MAX_PTR)

#define GS_SYS_SEGMENT_SIZE(i) ((uchar*)GS_SYS_SEGMENT_TOP(i) - (uchar*)gs_sys_segments[i].base)

#define GS_SYS_SEGMENT_IS(i, ty) \
    ((i) >= 0 && (i) < gs_sys_num_segments && gs_sys_segments[i].type == ty)

gsumemorysize
gs_sys_memory_allocated_size()
{
    gsumemorysize res;
    int i;

    res = 0;
    for (i = 0; i < gs_sys_num_segments; i++)
        if (GS_SYS_SEGMENT_IS(i, gs_sys_segment_allocated))
            res += GS_SYS_SEGMENT_SIZE(i)
    ;

    return res;
}

int
gs_sys_memory_exhausted()
{
    int res;

    lock(&gs_allocator_lock);
    res =
        gs_sys_segments[gs_sys_num_segments - 1].type != gs_sys_segment_break
        || (uchar*)gs_sys_segments[gs_sys_num_segments - 1].base >= (uchar*)gs_sys_segments[0].base + 0x200 * 0x400 * 0x400
    ;
    unlock(&gs_allocator_lock);

    return res;
}

static gsumemorysize gs_sys_post_gc_memory_use;

int
gs_sys_should_gc()
{
    int res;

    lock(&gs_allocator_lock);
    res = gs_sys_memory_allocated_size() >= 2*gs_sys_post_gc_memory_use;
    unlock(&gs_allocator_lock);

    return res;
}

void
gs_sys_wait_for_gc()
{
    lock(&gs_allocator_lock);
    gs_sys_gc_num_procs_waiting++;
    unlock(&gs_allocator_lock);

again:
    lock(&gs_allocator_lock);
    if (gs_sys_gc_num_procs_waiting < gs_sys_num_procs) {
        unlock(&gs_allocator_lock);
        sleep(1);
        goto again;
    }
    unlock(&gs_allocator_lock);
}

#define MAX_GS_SYS_GC_NUM_PRE_CALLBACKS 0x100
static int gs_sys_gc_num_pre_callbacks;
static gs_sys_gc_pre_callback *gs_sys_gc_pre_callbacks[MAX_GS_SYS_GC_NUM_PRE_CALLBACKS];

#define MAX_GS_SYS_GC_NUM_ROOT_CALLBACKS 0x100
static int gs_sys_gc_num_root_callbacks;
static gs_sys_gc_root_callback *gs_sys_gc_root_callbacks[MAX_GS_SYS_GC_NUM_ROOT_CALLBACKS];

int
gs_sys_start_gc(struct gsstringbuilder *err)
{
    int i;

    for (i = 0; i < gs_sys_gc_num_pre_callbacks; i++) gs_sys_gc_pre_callbacks[i]();

    for (i = 0; i < gs_sys_num_segments; i++) {
        if (gs_sys_segments[i].type == gs_sys_segment_allocated)
            gs_sys_segments[i].type = gs_sys_segment_gc_from_space
        ;
    }

    gs_sys_in_gc = 1;

    for (i = 0; i < gs_sys_gc_num_root_callbacks; i++) {
        if (gs_sys_gc_root_callbacks[i](err) < 0)
            return -1
        ;
    }

    return 0;
}

int
gs_gc_trace_pos(struct gsstringbuilder *err, struct gspos *pos)
{
    if (gs_gc_trace_interned_string(err, &pos->file) < 0)
        return -1
    ;
    return 0;
}

int
gs_sys_block_in_gc_from_space(void *p)
{
    int i;

    if (!gs_sys_in_gc) return 0;
    for (i = 0; i < gs_sys_num_segments; i++) {
        if (
            gs_sys_segments[i].type == gs_sys_segment_gc_from_space
            && (uchar*)gs_sys_segments[i].base <= (uchar*)p
            && (uchar*)p < (uchar*)GS_SYS_SEGMENT_TOP(i)
        ) {
            unlock(&gs_allocator_lock);
            return 1;
        }
    }
    return 0;
}

void
gs_sys_gc_pre_callback_register(gs_sys_gc_pre_callback *cb)
{
    if (gs_sys_gc_num_pre_callbacks >= MAX_GS_SYS_GC_NUM_PRE_CALLBACKS)
        gswarning(UNIMPL("Out of gs_sys_gc_pre_callbacks"))
    ;
    gs_sys_gc_pre_callbacks[gs_sys_gc_num_pre_callbacks++] = cb;
}

void
gs_sys_gc_root_callback_register(gs_sys_gc_root_callback *cb)
{
    if (gs_sys_gc_num_root_callbacks >= MAX_GS_SYS_GC_NUM_ROOT_CALLBACKS)
        gswarning(UNIMPL("Out of gs_sys_gc_root_callbacks"))
    ;
    gs_sys_gc_root_callbacks[gs_sys_gc_num_root_callbacks++] = cb;
}

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

static struct gs_sys_global_block_suballoc_info *gs_sys_first_global_block_suballoc_info;
static gs_sys_gc_pre_callback gs_sys_first_global_block_suballoc_gc_pre_callback;

void *
gs_sys_global_block_suballoc(struct gs_sys_global_block_suballoc_info *info, ulong sz)
{
    void *res;
    ulong align;

    align = info->align;
    if (!align) align = sizeof(void*);

    lock(&info->lock);
        if (!info->next) {
            lock(&gs_allocator_lock);
            if (gs_sys_first_global_block_suballoc_info) {
                info->next = gs_sys_first_global_block_suballoc_info->next;
                gs_sys_first_global_block_suballoc_info->next = info;
            } else {
                info->next = gs_sys_first_global_block_suballoc_info = info;
                gs_sys_gc_pre_callback_register(gs_sys_first_global_block_suballoc_gc_pre_callback);
            }
            unlock(&gs_allocator_lock);
        }

        res = gs_sys_block_suballoc(&info->descr, &info->nursury, sz, align);
    unlock(&info->lock);

    return res;
}

void
gs_sys_first_global_block_suballoc_gc_pre_callback()
{
    struct gs_sys_global_block_suballoc_info *info;

    info = gs_sys_first_global_block_suballoc_info;
    do {
        info->nursury = 0;
        info = info->next;
    } while (info != gs_sys_first_global_block_suballoc_info);
}
