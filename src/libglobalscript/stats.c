#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsstats.h"

static int gsstats_fd;

void
gstat_initialize(char *doc)
{
    struct gsstringbuilder *buf;
    char *basename;

    if (!gsflag_stat_collection) return;

    basename = strrchr(doc, '/');
    if (basename) basename++; else basename = doc;

    buf = gsreserve_string_builder();
    gsstring_builder_print(buf, "%s.stats", basename);
    gsfinish_string_builder(buf);

    gsstats_fd = create(buf->start, OWRITE | OTRUNC | OCEXEC, 0644);
    if (gsstats_fd < 0) gsfatal("Couldn't open %s: %r", buf->start);
}

void
gsstatprint(char *msg, ...)
{
    va_list arg;

    if (!gsflag_stat_collection) return;

    va_start(arg, msg);
    vfprint(gsstats_fd, msg, arg);
    va_end(arg);
}
