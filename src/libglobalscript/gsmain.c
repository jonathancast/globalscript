#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "ace.h"

#define FETCH_OPTION() \
    (cur_arg = *++argv, --argc, is_option = (*cur_arg == '-'), (is_option ? cur_arg++ : 0)) \

void
gsmain(int argc, char **argv)
{
    argv0 = *argv;
    int is_option = 0;
    char *cur_arg = *argv;
    gsvalue entry_point = 0;
    gsassert(
        __FILE__, __LINE__,
        sizeof(struct gs_blockdesc) == sizeof(gsvalue),
        "sizeof(struct gs_blockdesc) is %x, should be %x",
        sizeof(struct gs_blockdesc),
        sizeof(gsvalue)
    );
    FETCH_OPTION();
    while (argc) {
        if (!*cur_arg) FETCH_OPTION();
        if (!argc) break;
        if (is_option) {
            gsfatal("Invalid option flag %c", *cur_arg);
        } else {
            gsinputheader hdr;
            if (gsisdir(cur_arg)) {
                gsadddir(cur_arg);
            } else {
                gsfiletype ft = gsloadfile(cur_arg, &hdr, &entry_point);
                switch (ft) {
                    case gsfiledocument:
                        goto have_document;
                    case gsfileerror:
                        gswarning("%s: non-fatal error when reading file", cur_arg);
                        break;
                    default:
                        gsfatal("%s: loaded unknown file type %d", cur_arg, ft);
                }
            }
            FETCH_OPTION();
        }
    }
    gsfatal("gsmain at end of command-line arguments next");
    exits("no-document");
have_document:
    if (!gsentrypoint)
        gsfatal("Do not in fact have a document; check gsloadfile");
    if (ace_init() < 0)
        gsfatal("ace_init failed: %r");
    GS_SLOW_EVALUATE(gsentrypoint);
    gsrun(gsentrypoint);
    exits("");
}

