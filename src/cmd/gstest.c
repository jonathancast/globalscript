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
    test_failed,
};

static struct gstype *test_property_type(struct gspos);
static struct gstype *test_expected_property_structure(struct gspos);

static enum test_state test_evaluate(char *, char *, gsvalue);
static void test_print(int, gsvalue, int);

void
gsrun(char *doc, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *type, int argc, char **argv)
{
    enum test_state st;
    char err[0x100];

    if (gstypes_type_check(err, err + sizeof(err), pos, type, test_property_type(pos)) < 0) {
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
                test_print(1, prog, 1);
                ace_down();
                exits("");
            case test_failed:
                print("%s: Failed:\n", doc);
                test_print(1, prog, 1);
                ace_down();
                exits("failed");
            default:
                fprint(2, "%s:%d: Handle test state %d next\n", __FILE__, __LINE__, st);
                ace_down();
                exits("unimpl");
        }
    }
}

static struct gstype *test_string_type(struct gspos);

enum { /* Keep in sync with the below */
    test_property_constr_false,
    test_property_constr_label,
    test_property_constr_true,
    test_property_constr_and,
};

static
struct gstype *
test_expected_property_structure(struct gspos pos)
{
    return gstypes_compile_lift(pos, gstypes_compile_sum(pos, 4,
        gsintern_string(gssymconstrlable, "false"), gstypes_compile_ubproduct(pos, 1,
            gsintern_string(gssymfieldlable, "0"), test_string_type(pos)
        ),
        gsintern_string(gssymconstrlable, "label"), gstypes_compile_ubproduct(pos, 2,
            gsintern_string(gssymfieldlable, "0"), test_string_type(pos),
            gsintern_string(gssymfieldlable, "1"), test_property_type(pos)
        ),
        gsintern_string(gssymconstrlable, "true"), gstypes_compile_ubproduct(pos, 1,
            gsintern_string(gssymfieldlable, "0"), test_string_type(pos)
        ),
        gsintern_string(gssymconstrlable, "∧"), gstypes_compile_ubproduct(pos, 2,
            gsintern_string(gssymfieldlable, "0"), test_property_type(pos),
            gsintern_string(gssymfieldlable, "1"), test_property_type(pos)
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
        case gstyindir:
            return test_evaluate(err, eerr, GS_REMOVE_INDIRECTIONS(v));
        case gstyerr:
            gserror_format(err, eerr, (struct gserror *)v);
            return test_prog_err;
        case gstyimplerr:
            gsimplementation_failure_format(err, eerr, (struct gsimplementation_failure *)v);
            return test_impl_err;
        default:
            seprint(err, eerr, UNIMPL_NL("test_evaluate: st = %d"), st);
            return test_impl_err;
    }
}

static
enum test_state
test_evaluate_constr(char *err, char *eerr, struct gsconstr *constr)
{
    switch (constr->constrnum) {
        case test_property_constr_false:
            return test_failed;
        case test_property_constr_label:
            return test_evaluate(err, eerr, constr->arguments[1]);
        case test_property_constr_true:
            return test_succeeded;
        case test_property_constr_and: {
            enum test_state st;

            st = test_evaluate(err, eerr, constr->arguments[0]);
            switch (st) {
                case test_running:
                    return st;
                case test_succeeded:
                    return test_evaluate(err, eerr, constr->arguments[1]);
                default:
                    seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: ∧: st0 = %d"), st);
                    return test_impl_err;
            }
            break;
        }
        default:
            seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: constr = %d"), constr->constrnum);
            return test_impl_err;
    }
}

static void test_indent(int);
static void test_print_label(int, gsvalue);
static void test_print_failure(int, gsvalue);

static
void
test_print(int depth, gsvalue v, int print_if_trivial)
{
    char err[0x100];
    gstypecode st;

    st = GS_SLOW_EVALUATE(v);

    switch (st) {
        case gstyindir:
            test_print(depth, GS_REMOVE_INDIRECTIONS(v), print_if_trivial);
            return;
        case gstywhnf: {
            struct gsconstr *constr;

            constr = (struct gsconstr *)v;
            switch (constr->constrnum) {
                case test_property_constr_false:
                    test_print_failure(depth, constr->arguments[0]);
                    return;
                case test_property_constr_label:
                    test_print_label(depth, constr->arguments[0]);
                    test_print(depth + 1, constr->arguments[1], 1);
                    return;
                case test_property_constr_true:
                    if (print_if_trivial) {
                        test_indent(depth);
                        print("Succeeded\n");
                    }
                    return;
                case test_property_constr_and: {
                    enum test_state st0, st1;
                    st0 = test_evaluate(err, err + sizeof(err), constr->arguments[0]);
                    st1 = test_evaluate(err, err + sizeof(err), constr->arguments[1]);
                    test_print(depth, constr->arguments[0], 0);
                    test_print(depth, constr->arguments[1], print_if_trivial);
                    return;
                }
                default:
                    fprint(2, UNIMPL_NL("test_print: constr = %d"), constr->constrnum);
                    ace_down();
                    exits("unimpl");
            }
            break;
        }
        default:
            fprint(2, UNIMPL_NL("test_print: st = %d"), st);
            ace_down();
            exits("unimpl");
    }
}

static void test_print_string(gsvalue);

static
void test_print_label(int depth, gsvalue s)
{
    test_indent(depth);
    test_print_string(s);
    print(":\n");
}

static
void
test_print_failure(int depth, gsvalue s)
{
    test_indent(depth);
    test_print_string(s);
    print("\n");
}

static
void
test_indent(int depth)
{
    int i;

    for (i = 0; i < depth; i++)
        print("    ")
    ;
}

static
void
test_print_string(gsvalue s)
{
    gsvalue c;
    gstypecode st;
    char err[0x100];

    c = 0;
    for (;;) {
        if (c) {
            st = GS_SLOW_EVALUATE(c);
            switch (st) {
                case gstyunboxed: {
                    char buf[5];
                    if (!gsrunetochar(c, buf, buf + sizeof(buf), err, err + sizeof(err))) {
                        fprint(2, "%s\n", err);
                        ace_down();
                        exits("unimpl");
                    }
                    print(buf);
                    c = 0;
                    break;
                }
                default:
                    fprint(2, UNIMPL_NL("test_print_label(st = %d)"), st);
                    ace_down();
                    exits("unimpl");
            }
        } else {
            st = GS_SLOW_EVALUATE(s);
            switch (st) {
                case gstywhnf: {
                    struct gsconstr *constr;
                    constr = (struct gsconstr *)s;
                    switch (constr->constrnum) {
                        case 0:
                            c = constr->arguments[0];
                            s = constr->arguments[1];
                            break;
                        case 1:
                            return;
                        default:
                            fprint(2, UNIMPL_NL("test_print_label(constr = %d)"), constr->constrnum);
                            ace_down();
                            exits("unimpl");
                    }
                    break;
                }
                default:
                    fprint(2, UNIMPL_NL("test_print_label(st = %d)"), st);
                    ace_down();
                    exits("unimpl");
            }
        }
    }
}

static
struct gstype *
test_string_type(struct gspos pos)
{
    return gstypes_compile_list(pos, gstypes_compile_rune(pos));
}

static
struct gstype *
test_property_type(struct gspos pos)
{
    return gstypes_compile_abstract(pos, gsintern_string(gssymtypelable, "test.property.t"), gskind_compile_string(pos, "*"));
}
