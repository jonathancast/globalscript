#include <u.h>
#define NOPLAN9DEFINES
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "iosysconstants.h"

char *
gsbio_efmt_iochannel_type(char *result, char *end, enum gsbio_iochannel_type ty)
{
    switch (ty) {
        case gsbio_iostat:
            return seprint(result, end, "gsbio_iostat");
        default:
            return seprint(result, end, "'Unk iochannel type': %d", ty);
    }
}

