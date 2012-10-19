#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

static int gsprint(gsvalue prog);

void
gsrun(char *doc, struct gsfile_symtable *symtable, gsvalue prog, struct gstype *type)
{
    gstypecode st;

    do {
        st = GS_SLOW_EVALUATE(prog);

        switch (st) {
            case gstywhnf:
                if (gsprint(prog) < 0) {
                    ace_down();
                    exits("error");
                }
                ace_down();
                return;
            case gstystack:
                sleep(1);
                break;
            case gstyenosys:
                print("nosys %r\n");
                ace_down();
                exits("unimpl");
            default:
                ace_down();
                gsfatal_unimpl(__FILE__, __LINE__, "gsrun: st = %d", st);
        }
    } while (1);

    gsfatal("%s:%d: gsrun next", __FILE__, __LINE__);
}

static
int
gsprint(gsvalue prog)
{
    struct gs_blockdesc *block;

    prog = gsremove_indirections(prog);

    block = BLOCK_CONTAINING(prog);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)prog;
        switch (p->type) {
            case gserror_undefined:
                print("%s %s:%d\n", "undefined", p->file->name, p->lineno);
                break;
            case gserror_generated:
                print("%s:%d: %s\n", p->file->name, p->lineno, p->message);
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "gsprint(error type = %d)", p->type);
        }
        return -1;
    } else if (gsisheap_block(block)) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)prog;
        switch (hp->type) {
            case gsclosure: {
                struct gsclosure *cl;
                int nfvs, ncfvs, ncargs;

                cl = (struct gsclosure *)hp;

                nfvs = 0;
                ncfvs = 0;
                ncargs = cl->code->numargs;
                if (nfvs < ncfvs + ncargs) {
                    print("<function>");
                    return 0;
                } else {
                    ace_down();
                    gsfatal_unimpl(__FILE__, __LINE__, "gsprint(heap; nfvs = %d, ncfvs = %d, ncargs = %d)", nfvs, ncfvs, ncargs);
                    return -1;
                }
            }
            default:
                ace_down();
                gsfatal_unimpl(__FILE__, __LINE__, "gsprint(heap; type = %d)", hp->type);
                return -1;
        }
    } else {
        ace_down();
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint(%s)", block->class->description);
        return -1;
    }
}
