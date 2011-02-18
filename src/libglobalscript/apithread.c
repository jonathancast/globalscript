#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

apithread
apithreadcreate(gsvalue prog, apithreadmain *threadmain, gsvalue *pres, int bindtoproc)
{
    gsfatal("apithreadcreate(prog, threadmain, pres, bindtoproc) next");
    return 0;
}

void
apibindtothread(apithread t)
{
    gsfatal("apibindtothread(t) next");
}
