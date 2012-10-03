#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsrun(gsvalue prog)
{
    gsfatal_unimpl(__FILE__, __LINE__, "gsrun");
}
