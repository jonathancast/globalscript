#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

gstypecode
gseval(gsvalue val)
{
    gsfatal("gseval(%x) next", val);

    return gstyenosys;
}

