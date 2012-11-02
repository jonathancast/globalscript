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

static int gsprint(struct gstype *type, struct gsfile_symtable *, gsvalue prog);
static int gsprint_unboxed(struct gstype *type, gsvalue prog);

void
gsrun(char *doc, struct gsfile_symtable *symtable, gsvalue prog, struct gstype *type)
{
    gstypecode st;

    do {
        st = GS_SLOW_EVALUATE(prog);

        switch (st) {
            case gstyindir:
                prog = gsremove_indirections(prog);
                break;
            case gstywhnf:
                if (gsprint(type, symtable, prog) < 0) {
                    ace_down();
                    exits("error");
                }
                ace_down();
                return;
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
gsprint(struct gstype *type, struct gsfile_symtable *symtable, gsvalue prog)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(prog);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)prog;
        switch (p->type) {
            case gserror_undefined:
                print("%s %P\n", "undefined", p->pos);
                break;
            case gserror_generated:
                print("%P: %s\n", p->pos, p->message);
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

                nfvs = cl->numfvs;
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
            } else {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Print constructors of type %d", type->pos, type->node);
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
int
gsprint_unboxed(struct gstype *type, gsvalue prog)
{
    char err[0x100];

    if (gstype_expect_prim(type, gsprim_type_defined, "rune.prim", "rune", err, err + sizeof(err)) >= 0) {
        char buf[5];

        if (!gsrunetochar(prog, buf, buf + sizeof(buf), err, err + sizeof(err))) {
            ace_down();
            gsfatal("%s", err);
        }

        print("%s\n", buf);
        return 0;
    } else {
        ace_down();
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint_unboxed");
        return -1;
    }
}
