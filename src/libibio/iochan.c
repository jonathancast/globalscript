#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "iosysconstants.h"
#include "iofile.h"

struct uxio_channel *
ibio_get_channel_for_external_io(enum ibio_iochannel_type ty)
{
    char s[0x100];
    ibio_efmt_iochannel_type(s, s+sizeof s, ty);
    gsfatal("ibio_get_channel_for_external_io(%d, %s) next", ty, s);
    return 0;
}
