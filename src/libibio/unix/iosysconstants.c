#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>
#include <libglobalscript.h>

#include "iosysconstants.h"

char *
ibio_efmt_iochannel_type(char *result, char *end, enum ibio_iochannel_type ty)
{
    switch (ty) {
    case ibio_iostat:
        return seprint(result, end, "ibio_iostat");
    default:
        return seprint(result, end, "'Unk iochannel type': %d", ty);
    }
}

