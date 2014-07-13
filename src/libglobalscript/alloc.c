/* §source.file{The Simple Segment Manager} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsproc.h"

Lock gs_allocator_lock;

enum gs_sys_block_type {
    gs_sys_block_break,
    gs_sys_block_free,
    gs_sys_block_allocated,
    gs_sys_block_pinned,
    gs_sys_block_not_ours,
    gs_sys_block_gc_from_space,
};

static enum gs_sys_block_type *blocks;
static void *bottom_of_data;
static void *what_we_think_is_break;

#define MAX_NUM_LEGAL_BLOCKS (BLOCK_SIZE / sizeof(*blocks))
#define MAX_NUM_AVAILABLE_BLOCKS (BLOCK_SIZE / ((uchar*)GS_MAX_PTR - (uchar*)bottom_of_data)
#define BLOCK_TYPE(p) (blocks[(uintptr)BLOCK_CONTAINING(p) / BLOCK_SIZE])

#define BLOCK_AT_INDEX(i) ((void*)(i * BLOCK_SIZE))
#define FIRST_BLOCK_INDEX ((uintptr)bottom_of_data / BLOCK_SIZE)
#define LAST_BLOCK_INDEX ((uintptr)what_we_think_is_break / BLOCK_SIZE)

/* OK, this is confusing.
   §ccode{gs_sys_gc_running} means main process is somewhere in the GC code.
   §ccode{gs_sys_in_gc} means there exist 'from space' segments.
*/
static int gs_sys_gc_running, gs_sys_gc_num_procs_in_gc;
static char *gs_sys_gc_error;
int gs_sys_num_procs = 1;
static int gs_sys_in_gc;

void
gs_sys_memory_init(void)
{
    int i = 0;

    if (blocks) gsfatal("gs_sys_memory_init called twice");

    bottom_of_data = sbrk(0);
    if ((uintptr)bottom_of_data % BLOCK_SIZE) {
        bottom_of_data = (uchar*)bottom_of_data + BLOCK_SIZE - ((uintptr)bottom_of_data % BLOCK_SIZE);
        if (brk(bottom_of_data) < 0) gsfatal("brk failed! %r");
    }
    if ((uintptr)bottom_of_data >= GS_MAX_PTR)
        gsfatal("Out of memory in gs_sys_memory_init")
    ;
    blocks = bottom_of_data;

    bottom_of_data = (uchar*)bottom_of_data + BLOCK_SIZE;
    if (brk(bottom_of_data) < 0) gsfatal("brk failed! %r");

    what_we_think_is_break = bottom_of_data;

    for (i = 0; i < FIRST_BLOCK_INDEX; i++) blocks[i] = gs_sys_block_not_ours;
}

gsumemorysize
gs_sys_memory_allocated_size()
{
    gsumemorysize res;
    int i;

    res = 0;
    for (i = FIRST_BLOCK_INDEX; i < LAST_BLOCK_INDEX; i++)
        if (blocks[i] == gs_sys_block_allocated) res += BLOCK_SIZE
    ;

    return res;
}

int
gs_sys_memory_exhausted()
{
    int res;

    lock(&gs_allocator_lock);
    res =
        (uintptr)what_we_think_is_break >= GS_MAX_PTR
        || gs_sys_memory_allocated_size() >= 0x200 * 0x400 * 0x400
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
    gs_sys_gc_running = 1;
    gs_sys_gc_num_procs_in_gc++;
    unlock(&gs_allocator_lock);

again:
    lock(&gs_allocator_lock);
    if (gs_sys_gc_num_procs_in_gc < gs_sys_num_procs) {
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

    for (i = FIRST_BLOCK_INDEX; i < LAST_BLOCK_INDEX; i++) {
        if (blocks[i] == gs_sys_block_allocated)
            blocks[i] = gs_sys_block_gc_from_space
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

#define GS_SYS_GC_MAX_NUM_POST_CALLBACKS 0x100
static int gs_sys_gc_num_post_callbacks;
static gs_sys_gc_post_callback *gs_sys_gc_post_callbacks[GS_SYS_GC_MAX_NUM_POST_CALLBACKS];

int
gs_sys_finish_gc(struct gsstringbuilder *err)
{
    int i;

    for (i = 0; i < gs_sys_gc_num_post_callbacks; i++)
        if (gs_sys_gc_post_callbacks[i](err) < 0)
            return -1
    ;

    for (i = FIRST_BLOCK_INDEX; i < LAST_BLOCK_INDEX; i++) {
        if (blocks[i] == gs_sys_block_gc_from_space) blocks[i] = gs_sys_block_free;
    }

    gs_sys_in_gc = 0;

    /* GC must have completely succeeded by this point */

    gs_sys_post_gc_memory_use = gs_sys_memory_allocated_size();

    lock(&gs_allocator_lock);
        gs_sys_gc_num_procs_in_gc--;
        gs_sys_gc_running = 0;
        gs_sys_gc_error = 0;
    unlock(&gs_allocator_lock);

again:
    lock(&gs_allocator_lock);
    if (gs_sys_gc_num_procs_in_gc) {
        unlock(&gs_allocator_lock);
        sleep(1);
        goto again;
    }
    unlock(&gs_allocator_lock);

    return 0;
}

#define GS_SYS_GC_MAX_NUM_FAILURE_CALLBACKS 0x100
static int gs_sys_gc_num_failure_callbacks;
static gs_sys_gc_failure_callback *gs_sys_gc_failure_callbacks[GS_SYS_GC_MAX_NUM_FAILURE_CALLBACKS];

void
gs_sys_gc_failed(char *err)
{
    int i;

    for (i = 0; i < gs_sys_gc_num_failure_callbacks; i++)
        gs_sys_gc_failure_callbacks[i]()
    ;

    for (i = FIRST_BLOCK_INDEX; i < LAST_BLOCK_INDEX; i++) {
        if (blocks[i] == gs_sys_block_gc_from_space) blocks[i] = gs_sys_block_allocated;
    }

    gs_sys_in_gc = 0;

    gs_sys_post_gc_memory_use = gs_sys_memory_allocated_size();

    lock(&gs_allocator_lock);
        gs_sys_gc_num_procs_in_gc--;
        gs_sys_gc_running = 0;
        gs_sys_gc_error = err;
    unlock(&gs_allocator_lock);

again:
    lock(&gs_allocator_lock);
    if (gs_sys_gc_num_procs_in_gc) {
        unlock(&gs_allocator_lock);
        sleep(1);
        goto again;
    }
    unlock(&gs_allocator_lock);
}

int
gs_sys_gc_want_collection()
{
    int res;

    lock(&gs_allocator_lock);
    res = gs_sys_gc_running;
    unlock(&gs_allocator_lock);

    return res;
}

int
gs_sys_wait_for_collection_to_finish(struct gsstringbuilder *err)
{
    lock(&gs_allocator_lock);
    gs_sys_gc_num_procs_in_gc++;
    unlock(&gs_allocator_lock);

again:
    lock(&gs_allocator_lock);
    if (gs_sys_gc_running) {
        unlock(&gs_allocator_lock);
        sleep(1);
        goto again;
    }
    unlock(&gs_allocator_lock);

    lock(&gs_allocator_lock);
    if (gs_sys_gc_error) {
        if (err) gsstring_builder_print(err, "%s", gs_sys_gc_error);
        unlock(&gs_allocator_lock);
        return -1;
    } else {
        unlock(&gs_allocator_lock);
        return 0;
    }
}

void
gs_sys_gc_done_with_collection()
{
    lock(&gs_allocator_lock);
    gs_sys_gc_num_procs_in_gc--;
    unlock(&gs_allocator_lock);
}

int
gs_sys_gc_allow_collection(struct gsstringbuilder *err)
{
    int res;

    if (!gs_sys_gc_want_collection()) return 0;
    res = gs_sys_wait_for_collection_to_finish(err);
    gs_sys_gc_done_with_collection();

    return res;
}

int
gs_sys_block_in_gc_from_space(void *p)
{
    if (!gs_sys_in_gc) return 0;
    if ((uintptr)p >= (uintptr)what_we_think_is_break) return 0;
    return BLOCK_TYPE(p) == gs_sys_block_gc_from_space;
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

void
gs_sys_gc_post_callback_register(gs_sys_gc_post_callback *cb)
{
    if (gs_sys_gc_num_post_callbacks >= GS_SYS_GC_MAX_NUM_POST_CALLBACKS)
        gswarning(UNIMPL("Out of gs_sys_gc_post_callbacks"))
    ;
    gs_sys_gc_post_callbacks[gs_sys_gc_num_post_callbacks++] = cb;
}

void
gs_sys_gc_failure_callback_register(gs_sys_gc_failure_callback *cb)
{
    if (gs_sys_gc_num_failure_callbacks >= GS_SYS_GC_MAX_NUM_FAILURE_CALLBACKS)
        gswarning(UNIMPL("Out of gs_sys_gc_failure_callbacks"))
    ;
    gs_sys_gc_failure_callbacks[gs_sys_gc_num_failure_callbacks++] = cb;
}

/* Needs to lock the program break */
void *
gs_sys_block_alloc(registered_block_class cl)
{
    int i, j;
    struct gs_blockdesc *pres;

    lock(&gs_allocator_lock);

    if (!blocks) gs_sys_memory_init();

    /* Try to find a free block (TODO: linear scan can probably be improved) */
    for (i = FIRST_BLOCK_INDEX; i < LAST_BLOCK_INDEX; i++) {
        if (blocks[i] == gs_sys_block_free) break;
    }

    /* If we don't have a free block, try to extend available memory */
    if (i == LAST_BLOCK_INDEX) {
        gsumemorysize old_memory_size, break_size, new_free_segment_size;
        void *actual_break;

        actual_break = sbrk(0);
        if ((uintptr)actual_break > (uintptr)what_we_think_is_break) {
            gswarning(UNIMPL("Break moved when we weren't looking at it, from %p to %p; proceeding as if it never happened"), what_we_think_is_break, actual_break);
        }

        old_memory_size = (uchar*)what_we_think_is_break - (uchar*)bottom_of_data;
        break_size = (uchar*)GS_MAX_PTR - (uchar*)what_we_think_is_break;
        new_free_segment_size = old_memory_size;
        
        if (break_size <= 0) gswarning(UNIMPL("We've reached gs_sys_block_alloc even though memory is exhausted"));
        if (new_free_segment_size == 0) new_free_segment_size = BLOCK_SIZE;
        if (new_free_segment_size >= break_size) new_free_segment_size = break_size;
        what_we_think_is_break = (uchar*)what_we_think_is_break + new_free_segment_size;
        if (brk(what_we_think_is_break) < 0)
            gswarning("Could not extend data segment: brk failed (allocated %p to %p, allocating %p)! %r", bottom_of_data, what_we_think_is_break, new_free_segment_size)
        ;
        for (j = i; j < LAST_BLOCK_INDEX; j++) blocks[j] = gs_sys_block_free;
    }

    blocks[i] = gs_sys_block_allocated;
    pres = i < LAST_BLOCK_INDEX ? BLOCK_AT_INDEX(i) : 0;
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

static struct gs_sys_aligned_block_suballoc_info *gs_sys_first_aligned_block_suballoc_info;
static gs_sys_gc_pre_callback gs_sys_aligned_block_suballoc_gc_pre_callback;

void
gs_sys_aligned_block_suballoc(struct gs_sys_aligned_block_suballoc_info *info, void **pbeg, void **pend)
{
    struct gs_blockdesc *nursury_block;
    void *bufbase, *buf, *bufextent;

    lock(&info->lock);

    if (info->nursury) {
        buf = info->nursury;
        nursury_block = BLOCK_CONTAINING(buf);
    } else {
        if (!info->next) {
            lock(&gs_allocator_lock);
            if (gs_sys_first_aligned_block_suballoc_info) {
                info->next = gs_sys_first_aligned_block_suballoc_info->next;
                gs_sys_first_aligned_block_suballoc_info->next = info;
            } else {
                info->next = gs_sys_first_aligned_block_suballoc_info = info;
                gs_sys_gc_pre_callback_register(gs_sys_aligned_block_suballoc_gc_pre_callback);
            }
            unlock(&gs_allocator_lock);
        }
        nursury_block = gs_sys_block_alloc(&info->descr);
        buf = START_OF_BLOCK(nursury_block);
    }
    bufbase = (uchar*)buf;
    if ((uintptr)bufbase % info->align)
        bufbase = (uchar*)bufbase - ((uintptr)bufbase % info->align)
    ;
    bufextent = (uchar*)bufbase + info->align;
    info->nursury = (uchar*)bufextent >= (uchar*)END_OF_BLOCK(nursury_block) ? 0 : bufextent;

    unlock(&info->lock);

    *pbeg = buf;
    if (pend) *pend = bufextent;
}

void
gs_sys_aligned_block_suballoc_gc_pre_callback()
{
    struct gs_sys_aligned_block_suballoc_info *info;

    info = gs_sys_first_aligned_block_suballoc_info;
    do {
        info->nursury = 0;
        info = info->next;
    } while (info != gs_sys_first_aligned_block_suballoc_info);
}
