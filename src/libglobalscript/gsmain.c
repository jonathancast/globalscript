#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"

#define FETCH_OPTION() \
    (cur_arg = *++argv, --argc, is_option = (*cur_arg == '-'), (is_option ? cur_arg++ : 0)) \

void
p9main(int argc, char **argv)
{
    gsfatal("%x", sizeof(blockheader));
    argv0 = *argv;
    int is_option = 0;
    char *cur_arg = *argv;
    gsvalue document = 0;
    FETCH_OPTION();
    while (argc) {
        if (!*cur_arg) FETCH_OPTION();
        if (!argc) break;
        if (is_option) {
            gsfatal("Invalid option flag %c", *cur_arg);
        } else {
            gsheader hdr;
            gsfiletype ft = gsloadfile(cur_arg, &hdr);
            switch (ft) {
                case gsfiledocument:
                    document = hdr.entry_point;
                    goto have_document;
                case gsfileerror:
                    gswarning("%s: non-fatal error when reading file");
                    break;
                default:
                    gsfatal("%s: loaded unknown file type %d", cur_arg, ft);
            }
        }
    }
    gsfatal("p9main at end of command-line arguments next");
have_document:
    gsfatal("p9main(argc, argv) next");
}

/* Based on p9p's sysfatal function */
void
gsfatal(char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    fprint(2, "%s: %s\n", argv0, buf);
    exits("fatal");
}

void
gswarning(char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    fprint(2, "%s: %s\n", argv0, buf);
}
