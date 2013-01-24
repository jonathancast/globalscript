#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gssetup.h"
#include "gsregtables.h"
#include "ace.h"

static int gsPfmt(Fmt *f);
static int gsyfmt(Fmt *f);

void
gsmain(int argc, char **argv)
{
    char *cur_arg;
    struct gsfile_symtable *symtable;
    char *docfilename;
    struct gspos gsentrypos;

    argv0 = *argv++, argc--;
    gsassert(
        __FILE__, __LINE__,
        sizeof(struct gs_blockdesc) == sizeof(gsvalue),
        "sizeof(struct gs_blockdesc) is %x, should be %x",
        sizeof(struct gs_blockdesc),
        sizeof(gsvalue)
    );

    fmtinstall('P', gsPfmt);
    fmtinstall('y', gsyfmt);

    symtable = 0;

    gsadd_global_script_prim_sets();
    gsadd_client_prim_sets();
    gsadd_global_gslib(&symtable);
    while (argc) {
        cur_arg = *argv++, argc--;
        if (*cur_arg == '-') {
            cur_arg++;
            if (!strcmp(cur_arg, "stat")) {
                gsflag_stat_collection = 1;
            } else {
                gsfatal("Invalid option flag %s", cur_arg);
            }
        } else {
            if (gsisdir(cur_arg)) {
                gsadddir(cur_arg, &symtable);
            } else {
                gsfiletype ft = gsaddfile(cur_arg, &symtable, &gsentrypos, &gsentrypoint, &gsentrytype);
                switch (ft) {
                    case gsfiledocument:
                        docfilename = cur_arg;
                        goto have_document;
                    case gsfileerror:
                        gswarning("%s: non-fatal error when reading file", cur_arg);
                        break;
                    default:
                        gsfatal("%s: loaded unknown file type %d", cur_arg, ft);
                }
            }
        }
    }
    gsfatal("gsmain at end of command-line arguments next");
    docfilename = 0;
    exits("no-document");
have_document:
    if (!gsentrypoint)
        gsfatal_unimpl(__FILE__, __LINE__, "Do not in fact have a document; check gsaddfile");
    if (ace_init() < 0)
        gsfatal("ace_init failed: %r");
    GS_SLOW_EVALUATE(gsentrypoint);
    gsrun(docfilename, symtable, gsentrypos, gsentrypoint, gsentrytype, argc, argv);
    exits("");
}

static
int
gsPfmt(Fmt *f)
{
    struct gspos pos;

    pos = va_arg(f->args, struct gspos);
    return fmtprint(f, "%s:%d", pos.file->name, pos.lineno);
}

static
int
gsyfmt(Fmt *f)
{
    gsinterned_string sym;

    sym = va_arg(f->args, gsinterned_string);
    return fmtprint(f, "%s", sym->name);
}
