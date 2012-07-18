#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "ace.h"

gsvalue gsentrypoint;

int
ace_init(void)
{
    gsfatal_unimpl(__FILE__, __LINE__, "ace_init next");
    return -1;
}
