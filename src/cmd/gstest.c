#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

int listing;

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

static enum test_state test_evaluate(char *, char *, struct gspos, gsvalue);
static void test_print(struct gspos, int, gsvalue, int);

void
gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *symtable)
{
    struct gsstringbuilder *err;
    struct gspos typos;

    typos = gstype_get_location(pos, symtable, gsintern_string(gssymtypelable, "test.property.t"));

    err = gsreserve_string_builder();
    if (gstypes_type_check(err, typos, gstype_get_definition(typos, symtable, test_property_type(pos)),
        test_expected_property_structure(typos)
    ) < 0) {
        gsfinish_string_builder(err);
        fprint(2, "test.property.t has the wrong structure :%s\n", err->start);
        exits("type err");
    }
    gsfinish_string_builder(err);
}

void
gscheck_program(char *doc, struct gsfile_symtable *symtable, struct gspos pos, struct gstype *type)
{
    struct gsstringbuilder *err;

    err = gsreserve_string_builder();
    if (gstypes_type_check(err, pos, type, test_property_type(pos)) < 0) {
        gsfinish_string_builder(err);
        fprint(2, "Type error: %s\n", err->start);
        exits("type err");
    }
    gsfinish_string_builder(err);
}

int
gs_client_pre_ace_gc_trace_roots(struct gsstringbuilder *err)
{
    return 0;
}

void
gsrun(char *doc, struct gspos pos, gsvalue prog, int argc, char **argv)
{
    enum test_state st;
    char errbuf[0x100];

    while (argc) {
        switch (**argv) {
            default:
                fprint(2, "Can't parse command-line argument %s\n", *argv);
                ace_down();
                exits("arg parse err");
        }
    }

    for (;;) {
        st = test_evaluate(errbuf, errbuf + sizeof(errbuf), pos, prog);
        switch (st) {
            case test_impl_err:
                fprint(2, "%s: Implementation Limitation\n", doc);
                test_print(pos, 1, prog, 1);
                ace_down();
                exits("unimpl");
            case test_prog_err:
                fprint(2, "%s: Program Error:\n", doc);
                test_print(pos, 1, prog, 1);
                ace_down();
                exits("test err");
            case test_running:
                sleep(1);
                break;
            case test_succeeded:
                if (listing) {
                    print("%s:\n", doc);
                    test_print(pos, 1, prog, 1);
                }
                ace_down();
                exits("");
            case test_failed:
                print("%s: Failed:\n", doc);
                test_print(pos, 1, prog, 1);
                ace_down();
                exits("failed");
            default:
                fprint(2, UNIMPL("Handle test state %d next\n"), st);
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
test_evaluate(char *err, char *eerr, struct gspos pos, gsvalue v)
{
    gstypecode st;

    st = GS_SLOW_EVALUATE(pos, v);

    switch (st) {
        case gstystack:
            return test_running;
        case gstywhnf:
            return test_evaluate_constr(err, eerr, (struct gsconstr *)v);
        case gstyindir:
            return test_evaluate(err, eerr, pos, GS_REMOVE_INDIRECTION(pos, v));
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
    switch (constr->a.constrnum) {
        case test_property_constr_false:
            return test_failed;
        case test_property_constr_label:
            return test_evaluate(err, eerr, constr->pos, constr->a.arguments[1]);
        case test_property_constr_true:
            return test_succeeded;
        case test_property_constr_and: {
            enum test_state st;

            st = test_evaluate(err, eerr, constr->pos, constr->a.arguments[0]);
            switch (st) {
                case test_impl_err:
                case test_prog_err: {
                    enum test_state st1 = test_evaluate(err, eerr, constr->pos, constr->a.arguments[1]);
                    switch (st1) {
                        case test_impl_err:
                        case test_prog_err:
                        case test_running:
                            return st1;
                        case test_succeeded:
                        case test_failed:
                            return st;
                        default:
                            seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: ∧: st1 = %d"), st1);
                            return test_impl_err;
                    }
                    break;
                }
                case test_running:
                    return st;
                case test_succeeded:
                    return test_evaluate(err, eerr, constr->pos, constr->a.arguments[1]);
                case test_failed: {
                    enum test_state st1 = test_evaluate(err, eerr, constr->pos, constr->a.arguments[1]);
                    switch (st1) {
                        case test_impl_err:
                        case test_prog_err:
                        case test_running:
                            return st1;
                        case test_succeeded:
                        case test_failed:
                            return test_failed;
                        default:
                            seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: ∧: st1 = %d"), st1);
                            return test_impl_err;
                    }
                    break;
                }
                default:
                    seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: ∧: st = %d"), st);
                    return test_impl_err;
            }
            break;
        }
        default:
            seprint(err, eerr, UNIMPL_NL("test_evaluate_constr: constr = %d"), constr->a.constrnum);
            return test_impl_err;
    }
}

static void test_indent(int);
static void test_print_label(struct gspos, int, gsvalue);
static void test_print_failure(struct gspos, int, gsvalue);

static
void
test_print(struct gspos pos, int depth, gsvalue v, int print_if_trivial)
{
    char err[0x100];
    gstypecode st;

    st = GS_SLOW_EVALUATE(pos, v);

    switch (st) {
        case gstyindir:
            test_print(pos, depth, GS_REMOVE_INDIRECTION(pos, v), print_if_trivial);
            return;
        case gstywhnf: {
            struct gsconstr *constr;

            constr = (struct gsconstr *)v;
            switch (constr->a.constrnum) {
                case test_property_constr_false:
                    test_print_failure(constr->pos, depth, constr->a.arguments[0]);
                    return;
                case test_property_constr_label:
                    test_print_label(constr->pos, depth, constr->a.arguments[0]);
                    test_print(constr->pos, depth + 1, constr->a.arguments[1], 1);
                    return;
                case test_property_constr_true:
                    if (print_if_trivial) {
                        test_indent(depth);
                        print("Succeeded\n");
                    }
                    return;
                case test_property_constr_and: {
                    enum test_state st0, st1;
                    st0 = test_evaluate(err, err + sizeof(err), constr->pos, constr->a.arguments[0]);
                    st1 = test_evaluate(err, err + sizeof(err), constr->pos, constr->a.arguments[1]);
                    if (listing || st0 != test_succeeded)
                        test_print(constr->pos, depth, constr->a.arguments[0], 0)
                    ;
                    if (listing || st1 != test_succeeded)
                        test_print(constr->pos, depth, constr->a.arguments[1], print_if_trivial)
                    ;
                    return;
                }
                default:
                    fprint(2, UNIMPL_NL("test_print: constr = %d"), constr->a.constrnum);
                    ace_down();
                    exits("unimpl");
            }
            break;
        }
        case gstyerr: {
            char *s;

            s = gserror_format(err, err + sizeof(err), (struct gserror *)v);
            test_indent(depth);
            fprint(2, "%s\n", err);
            break;
        }
        case gstyimplerr: {
            char *s;

            s = gsimplementation_failure_format(err, err + sizeof(err), (struct gsimplementation_failure *)v);
            test_indent(depth);
            fprint(2, "%s\n", err);
            break;
        }
        default:
            fprint(2, UNIMPL_NL("test_print: st = %d"), st);
            ace_down();
            exits("unimpl");
    }
}

static void test_print_string(struct gspos, gsvalue);

static
void test_print_label(struct gspos pos, int depth, gsvalue s)
{
    test_indent(depth);
    test_print_string(pos, s);
    print(":\n");
}

static
void
test_print_failure(struct gspos pos, int depth, gsvalue s)
{
    test_indent(depth);
    test_print_string(pos, s);
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
test_print_string(struct gspos pos, gsvalue s)
{
    gsvalue c;
    gstypecode st;

    c = 0;
    for (;;) {
        if (c) {
            st = GS_SLOW_EVALUATE(pos, c);
            switch (st) {
                case gstystack:
                    sleep(1);
                    break;
                case gstyindir:
                    c = GS_REMOVE_INDIRECTION(pos, c);
                    break;
                case gstyunboxed: {
                    char buf[5];

                    gsrunetochar(c, buf, buf + sizeof(buf));
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
            st = GS_SLOW_EVALUATE(pos, s);
            switch (st) {
                case gstystack:
                    sleep(1);
                    break;
                case gstywhnf: {
                    struct gsconstr *constr;
                    constr = (struct gsconstr *)s;
                    switch (constr->a.constrnum) {
                        case 0:
                            c = constr->a.arguments[0];
                            s = constr->a.arguments[1];
                            break;
                        case 1:
                            return;
                        default:
                            fprint(2, UNIMPL_NL("test_print_label(constr = %d)"), constr->a.constrnum);
                            ace_down();
                            exits("unimpl");
                    }
                    break;
                }
                case gstyindir:
                    s = GS_REMOVE_INDIRECTION(pos, s);
                    break;
                case gstyerr: {
                    char buf[0x100];
                    gserror_format(buf, buf + sizeof(buf), (struct gserror *)s);
                    fprint(2, "%s\n", buf);
                    return;
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
