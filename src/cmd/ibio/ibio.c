#include <u.h>

#include <libc.h>

#include <libglobalscript.h>

#include "ibio.h"

void
gsrun(gsvalue prog)
{
    apithread t = apithreadcreate(prog, ibiothreadmain, 0, 1);
    apibindtothread(t);
}
