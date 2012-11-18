#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsadd_client_prim_sets()
{
}

enum test_state {
    test_impl_err,
    test_prog_err,
    test_running,
    test_succeeded,
};

static struct gstype *test_expected_property_structure(struct gspos);

static enum test_state test_evaluate(char *, char *, gsvalue);
static void test_print(int, struct gsconstr *);

void
gsrun(char *doc, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *type, int argc, char **argv)
{
    enum test_state st;
    char err[0x100];

    if (gstypes_type_check(err, err + sizeof(err), pos, type,
        gstypes_compile_abstract(pos, gsintern_string(gssymtypelable, "test.property.t"), gskind_compile_string(pos, "*"))
    ) < 0) {
        fprint(2, "Type error: %s\n", err);
        ace_down();
        exits("type err");
    }

    if (gstypes_type_check(err, err + sizeof(err), pos, gstype_get_definition(pos, symtable, type),
        test_expected_property_structure(pos)
    ) < 0) {
        fprint(2, "test.property.t has the wrong structure :%s\n", err);
        ace_down();
        exits("type err");
    }

    while (argc) {
        switch (**argv) {
            default:
                fprint(2, "Can't parse command-line argument %s\n", *argv);
                ace_down();
                exits("arg parse err");
        }
    }

    for (;;) {
        st = test_evaluate(err, err + sizeof(err), prog);
        switch (st) {
            case test_impl_err:
                fprint(2, "Error in gstest: %s\n", err);
                ace_down();
                exits("unimpl");
            case test_prog_err:
                fprint(2, "%s\n", err);
                ace_down();
                exits("test err");
            case test_running:
                sleep(1);
                break;
            case test_succeeded:
                print("%s:\n", doc);
                test_print(1, (struct gsconstr *)prog);
                ace_down();
                exits("");
            default:
                fprint(2, "%s:%d: Handle test state %d next\n", __FILE__, __LINE__, st);
                ace_down();
                exits("unimpl");
        }
    }
}

static struct gstype *test_string_type(struct gspos);

enum { /* Keep in sync with the below */
    test_property_constr_true,
};

static
struct gstype *
test_expected_property_structure(struct gspos pos)
{
    return gstypes_compile_lift(pos, gstypes_compile_sum(pos, 1,
        gsintern_string(gssymconstrlable, "true"), gstypes_compile_ubproduct(pos, 1,
            gsintern_string(gssymfieldlable, "0"), test_string_type(pos)
        )
    ));
}

static enum test_state test_evaluate_constr(char *, char *, struct gsconstr *);

static
enum test_state
test_evaluate(char *err, char *eerr, gsvalue v)
{
    gstypecode st;

    st = GS_SLOW_EVALUATE(v);

    switch (st) {
        case gstystack:
            return test_running;
        case gstywhnf:
            return test_evaluate_constr(err, eerr, (struct gsconstr *)v);
        case gstyerr:
            gserror_format(err, eerr, (struct gserror *)v);
            return test_prog_err;
        case gstyindir:
            return test_evaluate(err, eerr, GS_REMOVE_INDIRECTIONS(v));
        default:
            seprint(err, eerr, UNIMPL("test_evaluate: st = %d"), st);
            return test_impl_err;
    }
}

static
enum test_state
test_evaluate_constr(char *err, char *eerr, struct gsconstr *constr)
{
    switch (constr->constrnum) {
        case test_property_constr_true:
            return test_succeeded;
        default:
            seprint(err, eerr, UNIMPL("test_evaluate_constr: constr = %d"), constr->constrnum);
            return test_impl_err;
    }
}

static void test_indent(int);

static
void
test_print(int depth, struct gsconstr *constr)
{
    test_indent(depth);
    print("Succeeded\n");
}

void
test_indent(int depth)
{
    int i;

    for (i = 0; i < depth; i++)
        print("    ")
    ;
}

static
struct gstype *
test_string_type(struct gspos pos)
{
    return gstypes_compile_list(pos, gstypes_compile_rune(pos));
}
