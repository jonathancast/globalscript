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

static int gsprint(struct gspos pos, gsvalue prog);
static void gsprint_error(gsvalue prog);
static int gsprint_unboxed(gsvalue prog);

void
gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *symtable)
{
}

static struct {
    enum {
        gsprog_type_fun,
        gsprog_type_product,
        gsprog_type_sum,
        gsprog_type_rune,
        gsprog_type_natural,
    } type;
    union {
        struct {
            int numfields;
            gsinterned_string fields[MAX_NUM_REGISTERS];
        } product;
        struct {
            int numconstrs;
            gsinterned_string constrs[MAX_NUM_REGISTERS];
        } sum;
    };
} gsprog_type;

void
gscheck_program(char *doc, struct gsfile_symtable *symtable, struct gspos pos, struct gstype *type)
{
    int i;

again:
    switch (type->node) {
        case gstype_abstract:
            type = gstype_get_definition(pos, symtable, type);
            goto again;
        case gstype_lift: {
            struct gstype_lift *lift = (struct gstype_lift *)type;

            type = lift->arg;
            goto again;
        }
        case gstype_knprim: {
            struct gstype_knprim *prim;
            prim = (struct gstype_knprim *)type;
            if (!strcmp(prim->primset->name, "rune.prim") && !strcmp(prim->primname->name, "rune")) {
                gsprog_type.type = gsprog_type_rune;
            } else if (!strcmp(prim->primset->name, "natural.prim") && !strcmp(prim->primname->name, "natural")) {
                gsprog_type.type = gsprog_type_natural;
            } else {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: %P: Cannot print primitive %s %y", pos, type->pos, prim->primset->name, prim->primname);
            }
            break;
        }
        case gstype_app:
            type = gstype_get_definition(pos, symtable, type);
            goto again;
        case gstype_fun:
            gsprog_type.type = gsprog_type_fun;
            break;
        case gstype_sum: {
            struct gstype_sum *sum;
            sum = (struct gstype_sum *)type;
            gsprog_type.type = gsprog_type_sum;
            gsprog_type.sum.numconstrs = sum->numconstrs;
            for (i = 0; i < sum->numconstrs; i++)
                gsprog_type.sum.constrs[i] = sum->constrs[i].name
            ;
            break;
        }
        case gstype_product: {
            struct gstype_product *product;
            product = (struct gstype_product *)type;
            gsprog_type.type = gsprog_type_product;
            gsprog_type.product.numfields = product->numfields;
            for (i = 0; i < product->numfields; i++)
                gsprog_type.product.fields[i] = product->fields[i].name
            ;
            break;
        }
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: %P: Print values of type %d", pos, type->pos, type->node);
    }
}

int
gs_client_pre_ace_gc_trace_roots(struct gsstringbuilder *err)
{
    int i;

    switch (gsprog_type.type) {
        case gsprog_type_fun:
        case gsprog_type_natural:
            return 0;
        case gsprog_type_product:
            for (i = 0; i < gsprog_type.product.numfields; i++)
                if (gs_gc_trace_interned_string(err, &gsprog_type.product.fields[i]) < 0)
                    return -1
            ;
            return 0;
        case gsprog_type_sum:
            for (i = 0; i < gsprog_type.sum.numconstrs; i++)
                if (gs_gc_trace_interned_string(err, &gsprog_type.sum.constrs[i]) < 0)
                    return -1
            ;
            return 0;
        case gsprog_type_rune:
            return 0;
        default:
            gsstring_builder_print(err, UNIMPL("gs_client_pre_ace_gc_trace_roots: gsprog_type.type = %d"), gsprog_type.type);
            return -1;
    }
}

void
gsrun(char *doc, struct gspos pos, gsvalue prog, int argc, char **argv)
{
    gstypecode st;

    do {
        st = GS_SLOW_EVALUATE(pos, prog);

        switch (st) {
            case gstyindir:
                prog = GS_REMOVE_INDIRECTION(pos, prog);
                break;
            case gstywhnf:
                if (gsprint(pos, prog) < 0) {
                    ace_down();
                    exits("error");
                }
                ace_down();
                return;
            case gstyerr:
            case gstyimplerr:
                gsprint_error(prog);
                ace_down();
                exits("error");
            case gstyunboxed:
                if (gsprint_unboxed(prog) < 0) {
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
gsprint(struct gspos pos, gsvalue prog)
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
        struct gsrecord_fields *record;
        int i;

        record = (struct gsrecord_fields *)prog;
        if (gsprog_type.type != gsprog_type_product)
            gsfatal("%P: %P: Got a record, but not expecting to print a product", pos, record->rec.pos)
        ;
        print("〈");
        for (i = 0; i < record->numfields; i++) {
            if (i == 0)
                print(" ")
            ;
            print("%y = <expr>; ", gsprog_type.product.fields[i]);
        }
        print("〉\n");
        return 0;
    } else if (gsisconstr_block(block)) {
        struct gsconstr_args *constr;
        int i;

        constr = (struct gsconstr_args *)prog;
        if (gsprog_type.type != gsprog_type_sum)
            gsfatal("%P: %P: Got a construct, but not expecting to print a sum", pos, constr->c.pos)
        ;

        print("%y", gsprog_type.sum.constrs[constr->constrnum]);
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
gsprint_error(gsvalue prog)
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
gsprint_unboxed(gsvalue prog)
{
    char err[0x100];

    switch (gsprog_type.type) {
        case gsprog_type_rune: {
            char buf[5];

            gsrunetochar(prog, buf, buf + sizeof(buf));

            print("%s\n", buf);
            return 0;
        }
        case gsprog_type_natural: {
            char buf[24];

            if (!gsnaturaltochar(err, err + sizeof(err), prog, buf, buf + sizeof(buf))) {
                ace_down();
                gsfatal("Cannot print natural: %s", err);
                return -1;
            }

            print("%s\n", buf);
            return 0;
        }
        default:
            ace_down();
            gsfatal_unimpl(__FILE__, __LINE__, "gsprint_unboxed");
            return -1;
    }
}
