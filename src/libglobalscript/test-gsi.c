#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsrun(gsvalue prog)
{
    gsfatal("%s:%d: gsrun next", __FILE__, __LINE__);
}
