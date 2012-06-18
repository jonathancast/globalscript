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

gsvalue
gseval(gsvalue val)
{
    gsfatal("gseval(%x) next", val);

    return 0;
}

gsvalue gsnoeval(gsvalue val)
{
    gsfatal("Tried to evaluate a gsvalue which doesn't point into an evaluable block!");
    return 0;
}

struct gs_block_class gsbytecode_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte-code objects",
};

void *gsbytecode_nursury;
