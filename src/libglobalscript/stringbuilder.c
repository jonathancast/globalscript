/* §source.file Extensible Strings */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

static struct gs_block_class gsstringbuilder_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "String Builder Strings",
};
static void *gsstringbuilder_nursury;
static Lock gsstringbuilder_lock;
static int gsstringbuilder_gc_pre_callback_registered;
static gs_sys_gc_pre_callback gsstringbuilder_gc_pre_callback;

struct gsstringbuilder *
gsreserve_string_builder()
{
    struct gsstringbuilder *res;
    struct gs_blockdesc *block;

    lock(&gsstringbuilder_lock);
    if (gsstringbuilder_nursury) {
        res = gsstringbuilder_nursury;
        block = BLOCK_CONTAINING(gsstringbuilder_nursury);
        gsstringbuilder_nursury = 0;
    } else {
        if (!gsstringbuilder_gc_pre_callback_registered) {
            gs_sys_gc_pre_callback_register(gsstringbuilder_gc_pre_callback);
            gsstringbuilder_gc_pre_callback_registered = 1;
        }
        block = gs_sys_block_alloc(&gsstringbuilder_descr);
        res = START_OF_BLOCK(block);
    }
    unlock(&gsstringbuilder_lock);

    res->forward = 0;
    res->end = res->start = (char*)res + sizeof(*res);
    res->extent = END_OF_BLOCK(block);

    return res;
}

static
void
gsstringbuilder_gc_pre_callback()
{
    gsstringbuilder_nursury = 0;
}

int
gsextend_string_builder(struct gsstringbuilder *sb, ulong sz)
{
    if ((sb->extent - sb->end) < sz + 1)
        return -1
    ; else
        return 0
    ;
}

void
gsstring_builder_print(struct gsstringbuilder *buf, char *fmt, ...)
{
    va_list arg;

    va_start(arg, fmt);
    gsstring_builder_vprint(buf, fmt, arg);
    va_end(arg);
}

void
gsstring_builder_vprint(struct gsstringbuilder *buf, char *fmt, va_list arg)
{
    buf->end = vseprint(buf->end, buf->extent, fmt, arg);
}

void
gsfinish_string_builder(struct gsstringbuilder *sb)
{
    void *newnursury;

    *sb->end++ = 0;

    if (gs_sys_block_in_gc_from_space(BLOCK_CONTAINING(sb->end))) return;

    newnursury = (uintptr)sb->end % sizeof(void *) ? sb->end + (sizeof(void *) - (uintptr)sb->end % sizeof(void *)) : sb->end;

    if (sb->extent - sb->end >= 0x100) {
        lock(&gsstringbuilder_lock);
        if (gsstringbuilder_nursury) {
            struct gs_blockdesc *block;
            ulong sz;
            block = BLOCK_CONTAINING(gsstringbuilder_nursury);
            sz = (char*)END_OF_BLOCK(block) - (char*)gsstringbuilder_nursury;
            if (sz < sb->extent - (char*)newnursury)
                gsstringbuilder_nursury = newnursury
            ;
        } else {
            gsstringbuilder_nursury = newnursury;
        }
        unlock(&gsstringbuilder_lock);
    }
}

int
gsstring_builder_trace(struct gsstringbuilder *err, struct gsstringbuilder **psb)
{
    struct gsstringbuilder *sb, *newsb;

    sb = *psb;

    if (sb->forward) {
        *psb = sb->forward;
        return 0;
    }

    newsb = gsreserve_string_builder();

    memcpy(newsb->start, sb->start, sb->end - sb->start);
    newsb->end = newsb->start + (sb->end - sb->start);

    sb->forward = *psb = newsb;

    return 0;
}
