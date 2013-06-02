#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
gsstatprint(char *msg, ...)
{
    va_list arg;

    if (!gsflag_stat_collection) return;

    va_start(arg, msg);
    vfprint(2, msg, arg);
    va_end(arg);
}
