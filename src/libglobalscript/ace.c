#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "ace.h"

gsvalue gsentrypoint;

int
ace_init(void)
{
    gswarning("%s:%d: ace_init: deferred", __FILE__, __LINE__);
    return 0;
}
