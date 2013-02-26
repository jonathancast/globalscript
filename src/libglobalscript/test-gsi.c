#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsadd_client_prim_sets()
{
}

static int gsprint(struct gspos pos, struct gstype *type, struct gsfile_symtable *, gsvalue prog);
static void gsprint_error(struct gstype *type, struct gsfile_symtable *symtable, gsvalue prog);
static int gsprint_unboxed(struct gstype *type, gsvalue prog);

void
gsrun(char *doc, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *type, int argc, char **argv)
{
    gstypecode st;

    do {
        st = GS_SLOW_EVALUATE(prog);

        switch (st) {
            case gstyindir:
                prog = GS_REMOVE_INDIRECTION(prog);
                break;
            case gstywhnf:
                if (gsprint(pos, type, symtable, prog) < 0) {
                    ace_down();
                    exits("error");
                }
                ace_down();
                return;
            case gstyerr:
            case gstyimplerr:
                gsprint_error(type, symtable, prog);
                ace_down();
                exits("error");
            case gstyunboxed:
                if (gsprint_unboxed(type, prog) < 0) {
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
gsprint(struct gspos pos, struct gstype *type, struct gsfile_symtable *symtable, gsvalue prog)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(prog);

    if (gsisheap_block(block)) {
        struct gsheap_item *hp;

        hp = (struct gsheap_item *)prog;
        switch (hp->type) {
            case gsclosure: {
                struct gsclosure *cl;
                int nfvs, ncfvs, ncargs;

                cl = (struct gsclosure *)hp;

                nfvs = cl->numfvs;
                ncfvs = 0;
                ncargs = cl->code->numargs;
                if (nfvs < ncfvs + ncargs) {
                    print("<function>\n");
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
    } else if (gsisrecord_block(block)) {
        struct gsrecord *record;
        struct gstype_product *product;
        int i;

        product = 0;
        if (type->node == gstype_lift) {
            struct gstype_lift *lift = (struct gstype_lift *)type;

            type = lift->arg;
        }
        if (type->node == gstype_product) {
            product = (struct gstype_product *)type;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Print records of type %d", type->pos, type->node);
        }
        record = (struct gsrecord *)prog;
        print("〈");
        for (i = 0; i < record->numfields; i++) {
            if (i == 0)
                print(" ")
            ;
            print("%s = <expr>; ", product->fields[i].name->name);
        }
        print("〉\n");
        return 0;
    } else if (gsisconstr_block(block)) {
        struct gsconstr *constr;
        struct gstype_sum *sum;
        int i;

        constr = (struct gsconstr *)prog;

        sum = 0;
        while (type->node != gstype_sum) {
            if (type->node == gstype_app)
                type = gstype_get_definition(constr->pos, symtable, type)
            ; else if (type->node == gstype_lift) {
                struct gstype_lift *lift = (struct gstype_lift *)type;

                type = lift->arg;
            } else if (type->node == gstype_abstract) {
                type = gstype_get_definition(pos, symtable, type);
            } else {
                gsfatal(UNIMPL("%P: Print constructors of type %d"), type->pos, type->node);
            }
        }
        sum = (struct gstype_sum *)type;

        print("%y", sum->constrs[constr->constrnum].name);
        for (i = 0; i < constr->numargs; i++)
            print(" <expr>")
        ;
        print("\n");
        return 0;
    } else {
        ace_down();
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint(%s)", block->class->description);
        return -1;
    }
}

static
void
gsprint_error(struct gstype *type, struct gsfile_symtable *symtable, gsvalue prog)
{
    struct gs_blockdesc *block;
    char buf[0x100];

    block = BLOCK_CONTAINING(prog);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)prog;
        gserror_format(buf, buf + sizeof(buf), p);
        print("%s\n", buf);
    } else if (gsisimplementation_failure_block(block)) {
        struct gsimplementation_failure *p;
        char buf[0x100];

        p = (struct gsimplementation_failure *)prog;
        gsimplementation_failure_format(buf, buf + sizeof(buf), p);
        print("%s\n", buf);
    } else {
        ace_down();
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint_error(%s)", block->class->description);
    }
}

static
int
gsprint_unboxed(struct gstype *type, gsvalue prog)
{
    char err[0x100];
    struct gstype *tyw;

    if (gstype_expect_lift(err, err + sizeof(err), type, &tyw) >= 0)
        type = tyw
    ;
    if (
        gstype_expect_abstract(err, err + sizeof(err), type, "rune.t") >= 0
        || gstype_expect_prim(err, err + sizeof(err), type, gsprim_type_defined, "rune.prim", "rune") >= 0
    ) {
        char buf[5];

        gsrunetochar(prog, buf, buf + sizeof(buf));

        print("%s\n", buf);
        return 0;
    } else if (
        gstype_expect_abstract(err, err + sizeof(err), type, "natural.t") >= 0
        || gstype_expect_prim(err, err + sizeof(err), type, gsprim_type_defined, "natural.prim", "natural") >= 0
    ) {
        char buf[24];

        if (!gsnaturaltochar(err, err + sizeof(err), prog, buf, buf + sizeof(buf))) {
            ace_down();
            gsfatal("Cannot print natural: %s", err);
            return -1;
        }

        print("%s\n", buf);
        return 0;
    } else {
        ace_down();
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint_unboxed");
        return -1;
    }
}
