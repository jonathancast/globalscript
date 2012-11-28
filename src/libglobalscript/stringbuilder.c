/* §source.file Extensible Strings */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

static struct gs_block_class gsstringbuilder_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "String Builder Strings",
};
static void *gsstringbuilder_nursury;
static Lock gsstringbuilder_lock;

struct gsstringbuilder
gsreserve_string_builder()
{
    struct gsstringbuilder res;
    struct gs_blockdesc *block;

    lock(&gsstringbuilder_lock);
    if (gsstringbuilder_nursury) {
        res.start = gsstringbuilder_nursury;
        block = BLOCK_CONTAINING(gsstringbuilder_nursury);
        gsstringbuilder_nursury = 0;
    } else {
        block = gs_sys_seg_alloc(&gsstringbuilder_descr);
        res.start = START_OF_BLOCK(block);
    }
    unlock(&gsstringbuilder_lock);

    res.end = res.start;
    res.extent = END_OF_BLOCK(block);

    return res;
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
gsfinish_string_builder(struct gsstringbuilder *sb)
{
    *sb->end++ = 0;

    if (sb->extent - sb->end >= 0x100) {
        lock(&gsstringbuilder_lock);
        if (gsstringbuilder_nursury) {
            struct gs_blockdesc *block;
            ulong sz;
            block = BLOCK_CONTAINING(gsstringbuilder_nursury);
            sz = (char*)END_OF_BLOCK(block) - (char*)gsstringbuilder_nursury;
            if (sz < sb->extent - sb->end)
                gsstringbuilder_nursury = sb->end
            ;
        } else {
            gsstringbuilder_nursury = sb->end;
        }
        unlock(&gsstringbuilder_lock);
    }
}
