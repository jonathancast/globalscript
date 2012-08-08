#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsheap.h"

gstypecode
gs_get_gsvalue_state(gsvalue val)
{
    gsfatal("gs_get_gsvalue_state(%x) next", val);

    return 0;
}

gsvalue gsnoeval(gsvalue val)
{
    gsfatal("Tried to evaluate a gsvalue which doesn't point into an evaluable block!");
    return 0;
}

gsvalue
gsheapeval(gsvalue val)
{
    gsfatal_unimpl(__FILE__, __LINE__, "gsheapeval(%x)", val);

    return 0;
}

struct gs_block_class gsheap_descr = {
    /* evaluator = */ gsheapeval,
    /* description = */ "Global Script Heap",
};
static void *gsheap_nursury;

void *
gsreserveheap(ulong sz)
{
    return gs_sys_seg_suballoc(&gsheap_descr, &gsheap_nursury, sz, sizeof(struct gsbco*));
}

struct gs_block_class gsbytecode_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte-code objects",
};

void *gsbytecode_nursury;

void *
gsreservebytecode(ulong sz)
{
    return gs_sys_seg_suballoc(&gsbytecode_desc, &gsbytecode_nursury, sz, sizeof(void*));
}
