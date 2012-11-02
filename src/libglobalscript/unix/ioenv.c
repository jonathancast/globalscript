#include <u.h>
#define NOPLAN9DEFINES

#include <libc.h>

#include <libglobalscript.h>

#include "iosysconstants.h"
#include "../iofile.h"

struct uxio_ichannel *
gsbio_envvar_iopen(char *name)
{
    struct uxio_ichannel *chan;
    char *value;
    char *eov;
    void *dst;

    if (!(value = getenv(name)))
        return 0
    ;

    chan = gsbio_get_channel_for_external_io("", -1, gsbio_iogetenv);

    eov = value;
    while (*eov) eov++;

    dst = uxio_save_space(chan, eov - value);
    memcpy(dst, value, eov - value);

    return chan;
}

