#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsheap.h"

gstypecode
gs_get_gsvalue_state(gsvalue val)
{
    gsfatal("gs_get_gsvalue_state(%x) next", val);

    return 0;
}

gstypecode
gsnoeval(gsvalue val)
{
    gsfatal("Tried to evaluate a gsvalue which doesn't point into an evaluable block!");
    return 0;
}

static struct gsbco *gsheap_lock(struct gsclosure *);

gstypecode
gsheapeval(gsvalue val)
{
    gsfatal_unimpl(__FILE__, __LINE__, "gsheapeval(%x)", val);

    return 0;
}

gstypecode
gsevalunboxed(gsvalue val)
{
    return gstyunboxed;
}

gstypecode
gswhnfeval(gsvalue val)
{
    return gstywhnf;
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

struct gs_block_class gserrors_descr = {
    /* evaluator = */ gswhnfeval,
    /* description = */ "Erroneous Global Script Values",
};
static void *gserrors_nursury;

void *
gsreserveerrors(ulong sz)
{
    return gs_sys_seg_suballoc(&gserrors_descr, &gserrors_nursury, sz, sizeof(gsinterned_string));
}

int
gsiserror_block(struct gs_blockdesc *p)
{
    return p->class == &gserrors_descr;
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
