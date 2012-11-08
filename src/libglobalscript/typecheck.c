/* §source.file String-Code Type Checker */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsregtables.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

void
gstypes_process_type_declarations(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gskind **kinds, int n)
{
    static gsinterned_string gssymtyelimprim;

    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedline *ptype;

                    ptype = item.v;
                    if (gssymeq(ptype->directive, gssymtypedirective, ".tyabstract")) {
                        gsargcheck(ptype, 0, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[0]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyexpr")) {
                        if (ptype->numarguments > 0) {
                            kinds[i] = gskind_compile(ptype->pos, ptype->arguments[0]);

                            gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                        } else {
                            kinds[i] = 0;
                        }
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tydefinedprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyapiprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else {
                        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_process_type_declarations(%s)", ptype->pos, ptype->directive->name);
                    }
                }
                break;
            case gssymdatalable:
            case gssymcodelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal("%s:%d: %s:%d: gstypes_process_type_declarations(type = %d) next", __FILE__, __LINE__, item.v->pos.file->name, item.v->pos.lineno, item.type);
        }
    }
}

static void gstypes_kind_check_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);

void
gstypes_kind_check_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n)
{
    int i;

    for (i = 0; i < n; i++)
        gstypes_kind_check_item(symtable, items, types, kinds, n, i)
    ;
}

static
void
gstypes_kind_check_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    switch (items[i].type) {
        case gssymtypelable: {
            struct gstype *type;
            struct gskind *calculated_kind;

            type = types[i];

            calculated_kind = gstypes_calculate_kind(type);

            if (kinds[i]) {
                gstypes_kind_check_fail(type->pos, calculated_kind, kinds[i]);
            } else {
                kinds[i] = calculated_kind;
                gssymtable_set_type_expr_kind(symtable, items[i].v->label, calculated_kind);
            }
            return;
        }
        case gssymdatalable:
        case gssymcodelable:
        case gssymcoercionlable:
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_kind_check_item(type = %d)", items[i].v->pos, items[i].type);
    }
}

static void gstypes_kind_check_simple(struct gspos, struct gskind *);

struct gskind *
gstypes_calculate_kind(struct gstype *type)
{
    int i;

    switch (type->node) {
        case gstype_abstract: {
            struct gstype_abstract *abs;

            abs = (struct gstype_abstract *)type;
            if (abs->kind)
                return abs->kind;
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Abstract types without declared kinds", type->pos);
        }
        case gstype_knprim: {
            struct gspos pos;
            struct gstype_knprim *prim;
            struct gsregistered_primtype *primtype;

            prim = (struct gstype_knprim *)type;
            if (!(primtype = gsprims_lookup_type(prim->primset, prim->primname->name)))
                gsfatal("%P: Primset %s lacks a type member %s", type->pos, prim->primset->name, prim->primname->name)
            ;
            if (!primtype->kind)
                gsfatal_unimpl(__FILE__, __LINE__, "Panic! Primitype type %s (%s:%d) lacks a declared kind", primtype->name, primtype->file, primtype->line);
            pos.file = gsintern_string(gssymfilename, primtype->file);
            pos.lineno = primtype->line;
            return gskind_compile(pos, gsintern_string(gssymkindexpr, primtype->kind));
        }
        case gstype_unprim: {
            struct gstype_unprim *prim;

            prim = (struct gstype_unprim *)type;
            if (!prim->kind)
                gsfatal("%P: Primitive %s seems to lack a declared kind", type->pos, prim->primname->name);
            else
                return prim->kind
            ;
        }
        case gstype_var: {
            struct gstype_var *var;

            var = (struct gstype_var *)type;

            return var->kind;
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda;
            struct gskind *kybody;

            lambda = (struct gstype_lambda *)type;

            kybody = gstypes_calculate_kind(lambda->body);
            return gskind_exponential_kind(kybody, lambda->kind);
        }
        case gstype_forall: {
            struct gstype_forall *forall;
            struct gskind *kybody;

            forall = (struct gstype_forall *)type;

            kybody = gstypes_calculate_kind(forall->body);
            gstypes_kind_check_simple(type->pos, kybody);
            return kybody;
        }
        case gstype_lift: {
            struct gstype_lift *lift;
            struct gskind *kyarg;

            lift = (struct gstype_lift *)type;

            kyarg = gstypes_calculate_kind(lift->arg);
            gstypes_kind_check_fail(type->pos, kyarg, gskind_unlifted_kind());

            return gskind_lifted_kind();
        }
        case gstype_fun: {
            struct gstype_fun *fun;

            fun = (struct gstype_fun *)type;

            gstypes_kind_check_simple(type->pos, gstypes_calculate_kind(fun->tyarg));
            gstypes_kind_check_simple(type->pos, gstypes_calculate_kind(fun->tyres));

            return gskind_unlifted_kind();
        }
        case gstype_app: {
            struct gstype_app *app;
            struct gskind *funkind, *argkind;

            app = (struct gstype_app *)type;
            funkind = gstypes_calculate_kind(app->fun);
            argkind = gstypes_calculate_kind(app->arg);

            switch (funkind->node) {
                case gskind_exponential:
                    gstypes_kind_check_fail(type->pos, argkind, funkind->args[1]);
                    return funkind->args[0];
                case gskind_lifted:
                    gsfatal_bad_type(type->pos.file, type->pos.lineno, type, "Wrong kind: Expected ^, got *");
                default:
                    gsfatal_unimpl(__FILE__, __LINE__, "%P: 'function' kind (node = %d)", type->pos, funkind->node);
            }
        }
        case gstype_sum: {
            struct gstype_sum *sum;

            sum = (struct gstype_sum *)type;

            for (i = 0; i < sum->numconstrs; i++) {
                struct gskind *kind;

                kind = gstypes_calculate_kind(sum->constrs[i].argtype);
                gstypes_kind_check_simple(type->pos, kind);
                gsbc_typecheck_check_boxed_or_product(type->pos, sum->constrs[i].argtype);
            }

            return gskind_unlifted_kind();
        }
        case gstype_product: {
            struct gstype_product *prod;

            prod = (struct gstype_product *)type;

            for (i = 0; i < prod->numfields; i++) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: check kind of field type", type->pos);
            }

            return gskind_unlifted_kind();
        }
        case gstype_ubproduct: {
            struct gstype_ubproduct *prod;

            prod = (struct gstype_ubproduct *)type;

            for (i = 0; i < prod->numfields; i++) {
                struct gskind *kind;

                kind = gstypes_calculate_kind(prod->fields[i].type);
                gstypes_kind_check_simple(type->pos, kind);
                gsbc_typecheck_check_boxed(type->pos, prod->fields[i].type);
            }

            return gskind_unlifted_kind();
        }
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_calculate_kind(node = %d)", type->pos, type->node);
    }
    return 0;
}

void
gstypes_kind_check_fail(struct gspos pos, struct gskind *kyactual, struct gskind *kyexpected)
{
    char err[0x200];

    if (gstypes_kind_check(pos, kyactual, kyexpected, err, err + sizeof(err)) < 0)
        gsfatal("%s", err)
    ;
}

static char *seprint_kind_name(char *, char *, struct gskind *);

int
gstypes_kind_check(struct gspos pos, struct gskind *kyactual, struct gskind *kyexpected, char *err, char *eerr)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

    switch (kyexpected->node) {
        case gskind_unknown:
            if (kyactual->node != gskind_unknown && kyactual->node != gskind_unlifted && kyactual->node != gskind_lifted) {
                seprint(err, eerr, "%P: Incorrect kind: Expected '?'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_unlifted:
            if (kyactual->node != gskind_unlifted) {
                seprint(err, eerr, "%P: Incorrect kind: Expected 'u'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_lifted:
            if (kyactual->node != gskind_lifted) {
                seprint(err, eerr, "%P: Incorrect kind: Expected '*'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_exponential:
            if (kyactual->node != gskind_exponential) {
                seprint(err, eerr, "%P: Incorrect kind: Expected '^'; got '%s'", pos, actual_name);
                return -1;
            }
            if (gstypes_kind_check(pos, kyactual->args[0], kyexpected->args[0], err, eerr) < 0)
                return -1
            ;
            if (gstypes_kind_check(pos, kyactual->args[1], kyexpected->args[1], err, eerr) < 0)
                return -1
            ;
            return 0;
        default:
            seprint(err, eerr, "%s:%d: %P: gstypes_kind_check(expected = %d)", __FILE__, __LINE__, pos, kyexpected->node);
            return -1;
    }
}

static
void
gstypes_kind_check_simple(struct gspos pos, struct gskind *kyactual)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

    switch (kyactual->node) {
        case gskind_unlifted:
        case gskind_lifted:
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_kind_check_simple(actual = %s)", pos, actual_name);
    }
}

static
char *
seprint_kind_name(char *buf, char *ebuf, struct gskind *ky)
{
    switch (ky->node) {
        case gskind_unknown:
            return seprint(buf, ebuf, "?");
        case gskind_unlifted:
            return seprint(buf, ebuf, "u");
        case gskind_lifted:
            return seprint(buf, ebuf, "*");
        case gskind_exponential:
            buf = seprint_kind_name(buf, ebuf, ky->args[0]);
            buf = seprint_kind_name(buf, ebuf, ky->args[1]);
            buf = seprint(buf, ebuf, "^");
            return buf;
        default:
            return seprint(buf, ebuf, "%s:%d: Unknown kind type %d", __FILE__, __LINE__, ky->node);
    }
}

void
gsfatal_bad_type(gsinterned_string file, int lineno, struct gstype *ty, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    gsfatal("%s:%d: Bad type %P: %s", file->name, lineno, ty->pos, buf);
}

void
gsbc_typecheck_check_boxed(struct gspos pos, struct gstype *type)
{
    switch (type->node) {
        case gstype_knprim:
        case gstype_unprim:
        case gstype_var:
        case gstype_forall:
        case gstype_lift:
        case gstype_app:
        case gstype_fun:
        case gstype_product:
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: %P: gsbc_typecheck_check_boxed(node = %d)", pos, type->pos, type->node);
    }
}

void
gsbc_typecheck_check_boxed_or_product(struct gspos pos, struct gstype *type)
{
    switch (type->node) {
        case gstype_knprim:
        case gstype_unprim:
        case gstype_var:
        case gstype_forall:
        case gstype_lift:
        case gstype_app:
        case gstype_fun:
        case gstype_ubproduct:
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: %P: gsbc_typecheck_check_boxed_or_product(node = %d)", pos, type->pos, type->node);
    }
}

static void gstypes_process_data_type_signature(struct gsfile_symtable *, struct gsbc_item, struct gstype **);

void
gstypes_process_type_signatures(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **pentrytype, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymdatalable:
                gstypes_process_data_type_signature(symtable, items[i], pentrytype);
                break;
            case gssymcodelable:
            case gssymtypelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_process_data_type_signature(type = %d)", items[i].v->pos, items[i].type);
        }
    }
}

static
void
gstypes_process_data_type_signature(struct gsfile_symtable *symtable, struct gsbc_item item, struct gstype **pentrytype)
{
    static gsinterned_string gssymrecord, gssymconstr, gssymrune, gssymstring, gssymundefined, gssymclosure, gssymcast;

    struct gsparsedline *pdata;

    pdata = item.v;

    if (gssymceq(pdata->directive, gssymrecord, gssymdatadirective, ".record")) {
        return;
    } else if (
        gssymceq(pdata->directive, gssymrune, gssymdatadirective, ".rune")
        || gssymceq(pdata->directive, gssymstring, gssymdatadirective, ".string")
    ) {
        return;
    } else if (gssymceq(pdata->directive, gssymconstr, gssymdatadirective, ".constr")) {
        struct gstype *type;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal("%P: Couldn't find type %s", pdata->pos, pdata->arguments[0])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
        ; else if (pentrytype)
            *pentrytype = type
        ;
    } else if (gssymceq(pdata->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gstype *type;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal_bad_input(pdata, "Couldn't find type %s", pdata->arguments[0])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
        ; else if (pentrytype)
            *pentrytype = type
        ;
    } else if (gssymceq(pdata->directive, gssymclosure, gssymdatadirective, ".closure")) {
        struct gstype *type;

        if (pdata->numarguments < 2)
            return;

        type = gssymtable_get_type(symtable, pdata->arguments[1]);
        if (!type)
            gsfatal_bad_input(pdata, "Couldn't find type %s", pdata->arguments[1])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
        ; else if (pentrytype)
            *pentrytype = type
        ;
    } else if (gssymceq(pdata->directive, gssymcast, gssymdatadirective, ".cast")) {
        return;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_process_data_type_signature(%s)", item.v->pos, pdata->directive->name);
    }
}

static void gstypes_type_check_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, struct gstype **, int, int);

void
gstypes_type_check_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, struct gstype **pentrytype, int n)
{
    int i;

    for (i = 0; i < n; i++)
        gstypes_type_check_item(symtable, items, types, kinds, pentrytype, n, i)
    ;
}

static void gstypes_type_check_data_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, struct gstype **, int, int);
static void gstypes_type_check_code_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);
static void gstypes_type_check_coercion_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);

static
void
gstypes_type_check_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, struct gstype **pentrytype, int n, int i)
{
    switch (items[i].type) {
        case gssymtypelable:
            return;
        case gssymdatalable:
            gstypes_type_check_data_item(symtable, items, types, kinds, pentrytype, n, i);
            return;
        case gssymcodelable:
            gstypes_type_check_code_item(symtable, items, types, kinds, n, i);
            return;
        case gssymcoercionlable:
            gstypes_type_check_coercion_item(symtable, items, types, kinds, n, i);
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_kind_check_scc(type = %d)", items[i].v->pos, items[i].type);
    }
}

static void gstypes_type_check_type_fail(struct gspos pos, struct gstype *, struct gstype *);

static
void
gstypes_type_check_data_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, struct gstype **pentrytype, int n, int i)
{
    static gsinterned_string gssymrecord, gssymconstr, gssymrune, gssymstring, gssymundefined, gssymclosure, gssymcast;

    struct gsparsedline *pdata;

    pdata = items[i].v;
    if (gssymceq(pdata->directive, gssymrecord, gssymdatadirective, ".record")) {
        struct gstype *type;
        int j;
        struct gstype_field fields[MAX_NUM_REGISTERS];
        int numfields;

        numfields = 0;
        for (j = 0; j < pdata->numarguments; j += 2) {
            if (numfields >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many fields (max 0x%x)", pdata->pos, MAX_NUM_REGISTERS)
            ;
            fields[numfields].name = pdata->arguments[j];
            fields[numfields].type = gssymtable_get_data_type(symtable, pdata->arguments[j + 1]);
            if (!fields[numfields].type)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Can't find type of field value %s", pdata->pos, pdata->arguments[j + 1]->name)
            ;
            numfields++;
        }

        type = gstypes_compile_productv(pdata->pos, numfields, fields);

        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
        ; else if (pentrytype)
            *pentrytype = type
        ;
    } else if (gssymceq(pdata->directive, gssymconstr, gssymdatadirective, ".constr")) {
        struct gstype *type;
        struct gstype_sum *sum;
        struct gstype *argtype;
        int constrnum;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal("%P: Couldn't find type %s", pdata->pos, pdata->arguments[0])
        ;
        if (type->node != gstype_sum)
            gsfatal("%P: Type argument to .constr must be a sum type (not an abstract type)", pdata->pos)
        ;
        sum = (struct gstype_sum *)type;
        for (i = 0; i < sum->numconstrs; i++) {
            struct gstype_constr *constr, *prev_constr;

            constr = &sum->constrs[i];
            prev_constr = &sum->constrs[i - 1];
            if (i > 0 && strcmp(prev_constr->name->name, constr->name->name) == 0)
                gsfatal("%P: Duplicate constructor %s", pdata->pos, constr->name->name)
            ;
            if (i > 0 && strcmp(prev_constr->name->name, constr->name->name) > 0)
                gsfatal("%P: Constructors in the wrong order: %s should be after %s", pdata->pos, prev_constr->name->name, constr->name->name)
            ;
        }

        gsargcheck(pdata, 1, "constructor");
        for (i = 0; i < sum->numconstrs; i++) {
            if (sum->constrs[i].name == pdata->arguments[1]) {
                constrnum = i;
                argtype = sum->constrs[i].argtype;
                goto have_constr;
            }
        }
        gsfatal("%P: Type %s lacks a constructor %s", pdata->pos, pdata->arguments[0]->name, pdata->arguments[1]->name);
        argtype = 0;
    have_constr:
        if (pdata->numarguments == 3) {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Check constructor arguments (one argument)", pdata->pos);
        } else {
            struct gstype_ubproduct *ubproduct;

            if (argtype->node != gstype_ubproduct)
                gsfatal("%P: Type of the argument to %y in %y not an un-boxed product, therefore you cannot supply multiple argumentst to %y",
                    pdata->pos,
                    pdata->arguments[1],
                    pdata->arguments[0],
                    pdata->arguments[1]
                )
            ;
            ubproduct = (struct gstype_ubproduct *)argtype;
            if ((pdata->numarguments - 2) / 2 != ubproduct->numfields)
                gsfatal("%P: Wrong number of fields; got %d but expected %d", pdata->pos, (pdata->numarguments - 2) / 2, ubproduct->numfields)
            ;
            for (i = 0; 2+i < pdata->numarguments; i += 2) {
                struct gstype *fieldtype;

                if (i > 0 && strcmp(pdata->arguments[2+i-2]->name, pdata->arguments[2+i]->name) == 0)
                    gsfatal("%P: Duplicate field %y", pdata->pos, pdata->arguments[2+i])
                ;
                if (i > 0 && strcmp(pdata->arguments[2+i-2]->name, pdata->arguments[2+i]->name) > 0)
                    gsfatal("%P: Fields out of order; %y should come after %y", pdata->pos, pdata->arguments[2+i], pdata->arguments[2+i-2])
                ;
                if (pdata->arguments[2+i] != ubproduct->fields[i / 2].name)
                    gsfatal("%P: Wrong field #%d; got %y but expected %y", pdata->pos, i / 2, pdata->arguments[2+i], ubproduct->fields[i / 2].name)
                ;
                fieldtype = gssymtable_get_data_type(symtable, pdata->arguments[2+i+1]);
                gstypes_type_check_type_fail(pdata->pos, fieldtype, ubproduct->fields[i / 2].type);
            }
        }
    } else if (gssymceq(pdata->directive, gssymrune, gssymdatadirective, ".rune")) {
        struct gstype *rune;

        rune = gstypes_compile_prim(pdata->pos, gsprim_type_defined, "rune.prim", "rune", gskind_unlifted_kind());
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, rune)
        ; else if (pentrytype)
            *pentrytype = rune
        ;
    } else if (gssymceq(pdata->directive, gssymstring, gssymdatadirective, ".string")) {
        static gsinterned_string gssymtylist, gssymtyrune;
        struct gstype *list, *rune, *string;

         if (!gssymtylist)
             gssymtylist = gsintern_string(gssymtypelable, "list.t")
         ;
         list = gssymtable_get_type(symtable, gssymtylist);

         if (!gssymtyrune)
             gssymtyrune = gsintern_string(gssymtypelable, "rune.t")
         ;
         rune = gssymtable_get_type(symtable, gssymtyrune);

         string = gstype_apply(pdata->pos, list, rune);

         if (pdata->numarguments > 1)
             gstypes_type_check_type_fail(pdata->pos, gssymtable_get_data_type(symtable, pdata->arguments[1]), string)
         ;

        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, string)
        ; else if (pentrytype)
            *pentrytype = string
        ;
    } else if (gssymceq(pdata->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gstype *type;
        struct gskind *kind;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: couldn't find type '%s'", pdata->pos, pdata->arguments[0]->name)
        ;
        kind = gssymtable_get_type_expr_kind(symtable, pdata->arguments[0]);
        if (!kind)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: couldn't find kind of '%s'", pdata->pos, pdata->arguments[0]->name)
        ;
        gstypes_kind_check_fail(pdata->pos, kind, gskind_lifted_kind());
    } else if (gssymceq(pdata->directive, gssymclosure, gssymdatadirective, ".closure")) {
        struct gsbc_code_item_type *code_type;

        gsargcheck(pdata, 0, "code");
        code_type = gssymtable_get_code_type(symtable, pdata->arguments[0]);
        if (!code_type)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Cannot find type of code item %s", pdata->pos, pdata->arguments[0]->name)
        ;
        if (code_type->numftyvs)
            gsfatal_bad_input(pdata, "Code for a global data item cannot have free type variables")
        ;
        if (code_type->numfvs)
            gsfatal_bad_input(pdata, "Code for a global data item cannot have free variables")
        ;

        if (pdata->numarguments >= 2) {
            struct gstype *declared_type;

            declared_type = gssymtable_get_type(symtable, pdata->arguments[1]);
            gstypes_type_check_type_fail(pdata->pos, code_type->result_type, declared_type);
            gsbc_typecheck_check_boxed(pdata->pos, declared_type);
        } else {
            if (pdata->label)
                gssymtable_set_data_type(symtable, pdata->label, code_type->result_type)
            ; else if (pentrytype)
                *pentrytype = code_type->result_type
            ;
            gsbc_typecheck_check_boxed(pdata->pos, code_type->result_type);
        }
    } else if (gssymceq(pdata->directive, gssymcast, gssymdatadirective, ".cast")) {
        struct gstype *src_type;
        struct gsbc_coercion_type *coercion_type;

        coercion_type = gssymtable_get_coercion_type(symtable, pdata->arguments[0]);
        if (!coercion_type)
            gsfatal_bad_input(pdata, "Couldn't find type of coercion %s", pdata->arguments[0]->name);

        src_type = gssymtable_get_data_type(symtable, pdata->arguments[1]);
        if (!src_type)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Couldn't find type of %s", pdata->pos, pdata->arguments[1]->name);

        gstypes_type_check_type_fail(pdata->pos, src_type, coercion_type->source);

        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, coercion_type->dest)
        ; else if (pentrytype)
            *pentrytype = coercion_type->dest
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_type_check_data_item(%s)", pdata->pos, pdata->directive->name);
    }
}

static struct gsbc_code_item_type *gsbc_typecheck_code_expr(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *);
static struct gsbc_code_item_type *gsbc_typecheck_api_expr(struct gspos, struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, gsinterned_string, gsinterned_string);

static
void
gstypes_type_check_code_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    struct gsparsedline *pcode;
    struct gsparsedfile_segment *pseg;

    pseg = items[i].pseg;
    pcode = items[i].v;

    if (gssymeq(pcode->directive, gssymcodedirective, ".expr")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_code_expr(symtable, &pseg, gsinput_next_line(&pseg, pcode));
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else if (gssymeq(pcode->directive, gssymcodedirective, ".eprog")) {
        struct gsbc_code_item_type *type;

        if (pcode->numarguments < 1)
            gsfatal_bad_input(pcode, "Not enough arguments to .eprog; missing primset")
        ;
        if (pcode->numarguments < 2)
            gsfatal_bad_input(pcode, "Not enough arguments to .eprog; missing API monad name")
        ;
        type = gsbc_typecheck_api_expr(pcode->pos, symtable, &pseg, gsinput_next_line(&pseg, pcode), pcode->arguments[0], pcode->arguments[1]);
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_type_check_code_item(%s)", pcode->pos, pcode->directive->name);
    }
}

static struct gstype *gsbc_typecheck_check_api_statement_type(struct gspos, struct gstype *, gsinterned_string, gsinterned_string, int *);

static struct gs_block_class gsbc_code_type_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Code item types",
};
static void *gsbc_code_type_nursury;

enum gsbc_code_regtype {
    rttygvar,
    rttyarg,
    rtcode,
    rtgvar,
    rtfv,
    rtarg,
    rtlet,
    rtconts,
    rtgen,
};

struct gsbc_typecheck_code_or_api_expr_closure {
    enum gsbc_code_regtype regtype;

    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];

    /* §section Type registers */
    struct gstype *tyregs[MAX_NUM_REGISTERS];
    struct gskind *tyregkinds[MAX_NUM_REGISTERS];
    
    /* §section Sub-Expressions */
    int ncodes;
    gsinterned_string coderegs[MAX_NUM_REGISTERS];
    struct gsbc_code_item_type *codetypes[MAX_NUM_REGISTERS];

    /* §section Data registers */
    struct gstype *regtypes[MAX_NUM_REGISTERS];

    /* §section Arguments */
    int nargs;
    struct gstype *argtypes[MAX_NUM_REGISTERS];
    struct gsparsedline *arglines[MAX_NUM_REGISTERS];
    int arglifted[MAX_NUM_REGISTERS];
};

static int gsbc_typecheck_code_or_api_expr_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *);

static struct gstype *gsbc_typecheck_code_or_api_args(int, struct gstype **, struct gsparsedline **, int *, struct gstype *);

static struct gstype *gsbc_typecheck_compile_prim_type(struct gspos, struct gsfile_symtable *, char *);

static int gsbc_typecheck_free_type_variables(struct gsparsedline *, struct gsbc_code_item_type *);
static int gsbc_typecheck_free_variables(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gsbc_code_item_type *, int);

static
struct gsbc_code_item_type *
gsbc_typecheck_code_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    static gsinterned_string gssymtyarg, gssymgvar, gssymfv, gssymalloc, gssymrecord, gssymeprim, gssymlift, gssymapp, gssymenter, gssymundef;

    struct gsbc_typecheck_code_or_api_expr_closure cl;

    int i;

    int nfvs;
    gsinterned_string fvnames[MAX_NUM_REGISTERS];
    struct gspos fvposs[MAX_NUM_REGISTERS];
    struct gstype *fvtypes[MAX_NUM_REGISTERS];

    int ntyargs;
    gsinterned_string tyargnames[MAX_NUM_REGISTERS];
    struct gskind *tyargkinds[MAX_NUM_REGISTERS];
    struct gsparsedline *tyarglines[MAX_NUM_REGISTERS];

    int nconts;
    struct gsparsedline *contlines[MAX_NUM_REGISTERS];

    struct gstype *calculated_type;
    struct gskind *kind;
    struct gsbc_code_item_type *res;

    cl.nregs = cl.nargs = nfvs = cl.ncodes = nconts = ntyargs = 0;
    cl.regtype = rttygvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gsbc_typecheck_code_or_api_expr_op(symtable, p, &cl)) {
        } else if (gssymceq(p->directive, gssymtyarg, gssymcodeop, ".tyarg")) {
            struct gskind *argkind;

            if (cl.regtype > rttyarg)
                gsfatal_bad_input(p, "Too late to add arguments")
            ;
            cl.regtype = rttyarg;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            cl.regs[cl.nregs] = p->label;
            gsargcheck(p, 0, "kind");
            argkind = gskind_compile(p->pos, p->arguments[0]);

            cl.tyregkinds[cl.nregs] = argkind;
            cl.tyregs[cl.nregs] = gstypes_compile_type_var(p->pos, p->label, argkind);
            cl.nregs++;

            tyargnames[ntyargs] = p->label;
            tyargkinds[ntyargs] = argkind;
            tyarglines[ntyargs] = p;
            ntyargs++;
        } else if (gssymceq(p->directive, gssymgvar, gssymcodeop, ".gvar")) {
            if (cl.regtype > rtgvar)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            cl.regtype = rtgvar;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            cl.regs[cl.nregs] = p->label;
            cl.regtypes[cl.nregs] = gssymtable_get_data_type(symtable, p->label);
            if (!cl.regtypes[cl.nregs])
                gsfatal_bad_input(p, "Couldn't find type for global %s", p->label->name)
            ;
            cl.nregs++;
        } else if (gssymceq(p->directive, gssymfv, gssymcodeop, ".fv")) {
            int reg;
            struct gstype *fvtype;

            if (cl.regtype > rtfv)
                gsfatal_bad_input(p, "Too late to add free variables")
            ;
            cl.regtype = rtfv;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            cl.regs[cl.nregs] = p->label;
            gsargcheck(p, 0, "type");
            reg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[0]);
            fvtype = cl.tyregs[reg];
            if (!fvtype)
                gsfatal_bad_input(p, "Register %s is not a type", p->arguments[0]);
            for (i = 1; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                fvtype = gstype_apply(p->pos, fvtype, cl.tyregs[regarg]);
            }
            cl.regtypes[cl.nregs] = fvtype;
            cl.nregs++;

            fvtypes[nfvs] = fvtype;
            fvposs[nfvs] = p->pos;
            fvnames[nfvs] = p->label;
            nfvs++;
        } else if (gssymceq(p->directive, gssymalloc, gssymcodeop, ".alloc")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;
            int nftyvs;

            if (cl.regtype > rtlet)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            cl.regtype = rtlet;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, cl.coderegs, cl.ncodes, p->arguments[0]);
            cty = cl.codetypes[creg];
            calculated_type = cty->result_type;

            nftyvs = gsbc_typecheck_free_type_variables(p, cty);
            gsbc_typecheck_free_variables(&cl, p, cty, nftyvs);

            gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(calculated_type), gskind_lifted_kind());
            gsbc_typecheck_check_boxed(p->pos, calculated_type);

            cl.regs[cl.nregs] = p->label;
            cl.regtypes[cl.nregs] = calculated_type;

            cl.nregs++;
        } else if (gssymceq(p->directive, gssymrecord, gssymcodeop, ".record")) {
            struct gstype_field fields[MAX_NUM_REGISTERS];

            if (cl.regtype > rtlet)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            cl.regtype = rtlet;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;

            for (i = 0; i < p->numarguments; i += 2) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_code_expr(.record fields)", p->pos);
            }

            cl.regs[cl.nregs] = p->label;
            cl.regtypes[cl.nregs] = gstypes_compile_productv(p->pos, 0, fields);
            cl.nregs++;
        } else if (gssymceq(p->directive, gssymeprim, gssymcodeop, ".eprim")) {
            struct gsregistered_primset *prims;
            struct gstype *type;
            int tyreg;
            int first_arg_pos;

            if (cl.regtype > rtlet)
                gsfatal("%P: To late to add allocations", p->pos)
            ;
            cl.regtype = rtlet;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers", p->pos)
            ;

            gsargcheck(p, 3, "type");
            tyreg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[3]);
            type = cl.tyregs[tyreg];

            gsargcheck(p, 0, "primset");
            if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
                struct gsregistered_primtype *primty;
                struct gsregistered_prim *prim;
                struct gstype *expected_type;

                if (!(primty = gsprims_lookup_type(prims, p->arguments[1]->name)))
                    gsfatal("%P: Primitive set %s has no primtype %s", p->pos, prims->name, p->arguments[1]->name)
                ;

                if (!(prim = gsprims_lookup_prim(prims, p->arguments[2]->name)))
                    gsfatal("%P: Primitive set %s has no prim %s", p->pos, prims->name, p->arguments[2]->name)
                ;

                if (prim->group != gsprim_operation_api)
                    gsfatal("%P: Primitive %s in primset %s is not an API primitive", p->pos, prim->name, prims->name)
                ;

                if (strcmp(prim->apitype, primty->name))
                    gsfatal("%P: Primitive %s in primset %s does not belong to API type %s", p->pos, prim->name, prims->name, primty->name)
                ;

                expected_type = gsbc_typecheck_compile_prim_type(p->pos, symtable, prim->type);
                gstypes_type_check_type_fail(p->pos, type, expected_type);
            }

            for (i = 4; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
                int tyargreg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                type = gstype_instantiate(p->pos, type, cl.tyregs[tyargreg]);
            }
            if (i < p->numarguments) i++;
            first_arg_pos = i;
            for (; i < p->numarguments; i++) {
                int argreg;
                struct gstype *argtype;

                argreg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                argtype = cl.regtypes[argreg];

                if (type->node == gstype_fun) {
                    struct gstype_fun *fun;

                    fun = (struct gstype_fun *)type;
                    gstypes_type_check_type_fail(p->pos, argtype, fun->tyarg);
                    type = fun->tyres;
                } else {
                    gsfatal("%P: Too many arguments to %s (max %d; got %d)", p->pos, p->arguments[2]->name, i - first_arg_pos, p->numarguments - first_arg_pos);
                }
            }

            cl.regs[cl.nregs] = p->label;
            gsbc_typecheck_check_api_statement_type(p->pos, type, p->arguments[0], p->arguments[1], 0);
            cl.regtypes[cl.nregs] = type;
            cl.nregs++;
        } else if (
            gssymceq(p->directive, gssymlift, gssymcodeop, ".lift")
            || gssymceq(p->directive, gssymapp, gssymcodeop, ".app")
        ) {
            if (cl.regtype > rtconts)
                gsfatal_bad_input(p, "Too late to add continuations")
            ;
            cl.regtype = rtconts;
            if (nconts >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many continuations")
            ;
            contlines[nconts] = p;
            nconts++;
        } else if (gssymceq(p->directive, gssymenter, gssymcodeop, ".enter")) {
            int reg;
            struct gskind *kind;

            gsargcheck(p, 0, "var");
            reg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[0]);
            calculated_type = cl.regtypes[reg];
            for (i = 1; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                if (!cl.tyregs[regarg])
                    gsfatal("%P: %s doesn't seem to be a type register", p->pos, p->arguments[i]->name)
                ;
                calculated_type = gstype_instantiate(p->pos, calculated_type, cl.tyregs[regarg]);
            }

            kind = gstypes_calculate_kind(calculated_type);
            gstypes_kind_check_fail(p->pos, kind, gskind_lifted_kind());

            goto have_type;
        } else if (gssymeq(p->directive, gssymcodeop, ".yield")) {
            int reg;
            struct gskind *kind;

            gsargcheck(p, 0, "var");
            reg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[0]);
            calculated_type = cl.regtypes[reg];
            for (i = 1; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                if (!cl.tyregs[regarg])
                    gsfatal("%P: %s doesn't seem to be a type register", p->pos, p->arguments[i]->name)
                ;
                calculated_type = gstype_instantiate(p->pos, calculated_type, cl.tyregs[regarg]);
            }

            kind = gstypes_calculate_kind(calculated_type);
            gstypes_kind_check_fail(p->pos, kind, gskind_unlifted_kind());

            goto have_type;
        } else if (gssymceq(p->directive, gssymundef, gssymcodeop, ".undef")) {
            int reg;

            gsargcheck(p, 0, "type");
            reg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[0]);
            calculated_type = cl.tyregs[reg];
            kind = cl.tyregkinds[reg];
            for (i = 1; i < p->numarguments; i++) {
                int regarg;
                struct gstype *argtype;
                struct gskind *argkind;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                argtype = cl.tyregs[regarg];
                argkind = cl.tyregkinds[regarg];

                if (kind->node != gskind_exponential)
                    gsfatal("%P: Too many arguments to %s", p->pos, p->arguments[0]->name)
                ;

                gstypes_kind_check_fail(p->pos, argkind, kind->args[1]);

                calculated_type = gstype_apply(p->pos, calculated_type, argtype);
                kind = kind->args[0];
            }
            gstypes_kind_check_fail(p->pos, kind, gskind_lifted_kind());
            goto have_type;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_code_expr(%s)", p->pos, p->directive->name);
        }
    }

have_type:

    while (nconts--) {
        p = contlines[nconts];
        if (p->directive == gssymlift) {
            gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(calculated_type), gskind_unlifted_kind());
            calculated_type = gstypes_compile_lift(p->pos, calculated_type);
        } else if (p->directive == gssymapp) {
            for (i = 0; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                if (calculated_type->node == gstype_lift)
                    gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_code_expr (app of lifted fun)", p->pos)
                ;
                if (calculated_type->node == gstype_fun) {
                    struct gstype_fun *fun;

                    fun = (struct gstype_fun *)calculated_type;
                    gstypes_type_check_type_fail(p->pos, cl.regtypes[regarg], fun->tyarg);
                    calculated_type = fun->tyres;
                } else
                    gsfatal("%P: Too many arguments (max %d; got %d)", p->pos, i, p->numarguments)
                ;
            }
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_code_expr(cont %s)", p->pos, p->directive->name);
        }
    }

    calculated_type = gsbc_typecheck_code_or_api_args(cl.nargs, cl.argtypes, cl.arglines, cl.arglifted, calculated_type);

    while (ntyargs--) {
        p = tyarglines[ntyargs];

        for (i = 0; i < nfvs; i++)
            if (gstypes_is_ftyvar(tyargnames[ntyargs], fvtypes[i]))
                gsfatal("%P: Type argument %y is a free in the type of free variable %y", fvposs[i], tyargnames[ntyargs], fvnames[i])
        ;

        calculated_type = gstypes_compile_forall(p->pos, tyargnames[ntyargs], tyargkinds[ntyargs], calculated_type);
    }

    res = gs_sys_seg_suballoc(&gsbc_code_type_descr, &gsbc_code_type_nursury, sizeof(*res) + nfvs * sizeof(struct gstype *), sizeof(int));
    res->numftyvs = 0;
    res->numfvs = nfvs;
    res->fvtypes = (struct gstype **)((uchar*)res + sizeof(*res));
    for (i = 0; i < nfvs; i++)
        res->fvtypes[i] = fvtypes[i]
    ;
    res->result_type = calculated_type;

    return res;
}

static
struct gsbc_code_item_type *
gsbc_typecheck_api_expr(struct gspos pos, struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, gsinterned_string primsetname, gsinterned_string prim)
{
    struct gsbc_typecheck_code_or_api_expr_closure cl;

    int nbinds;
    int first_rhs_lifted;

    struct gstype *calculated_type;
    struct gsbc_code_item_type *res;

    cl.nregs = cl.ncodes = cl.nargs = nbinds = 0;
    cl.regtype = rttygvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gsbc_typecheck_code_or_api_expr_op(symtable, p, &cl)) {
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;
            int nftyvs;

            if (cl.regtype > rtgen)
                gsfatal_bad_input(p, "Too late to add generators")
            ;
            cl.regtype = rtgen;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, cl.coderegs, cl.ncodes, p->arguments[0]);
            cty = cl.codetypes[creg];
            calculated_type = cty->result_type;

            nftyvs = gsbc_typecheck_free_type_variables(p, cty);
            gsbc_typecheck_free_variables(&cl, p, cty, nftyvs);

            cl.regs[cl.nregs] = p->label;
            cl.regtypes[cl.nregs] = gsbc_typecheck_check_api_statement_type(p->pos, calculated_type, primsetname, prim, nbinds == 0 ? &first_rhs_lifted : 0);

            cl.nregs++;
            nbinds++;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;
            int nftyvs;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, cl.coderegs, cl.ncodes, p->arguments[0]);
            cty = cl.codetypes[creg];
            calculated_type = cty->result_type;

            nftyvs = gsbc_typecheck_free_type_variables(p, cty);
            gsbc_typecheck_free_variables(&cl, p, cty, nftyvs);

            gsbc_typecheck_check_api_statement_type(p->pos, calculated_type, primsetname, prim, nbinds == 0 ? &first_rhs_lifted : 0);
            goto have_type;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_api_expr(%s)", p->pos, p->directive->name);
        }
    }

have_type:

    if (first_rhs_lifted) {
        if (calculated_type->node != gstype_lift)
            calculated_type = gstypes_compile_lift(pos, calculated_type)
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Ensure API block statement has unlifted type", pos);
    }

    calculated_type = gsbc_typecheck_code_or_api_args(cl.nargs, cl.argtypes, cl.arglines, cl.arglifted, calculated_type);

    res = gs_sys_seg_suballoc(&gsbc_code_type_descr, &gsbc_code_type_nursury, sizeof(*res), sizeof(int));
    res->numftyvs = 0;
    res->numfvs = 0;
    res->result_type = calculated_type;

    return res;
}

static
int
gsbc_typecheck_code_or_api_expr_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymtygvar, gssymarg, gssymlarg;

    int i;

    if (gssymceq(p->directive, gssymtygvar, gssymcodeop, ".tygvar")) {
        if (pcl->regtype > rttygvar)
            gsfatal_bad_input(p, "Too late to add global type variables")
        ;
        pcl->regtype = rttygvar;
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal_bad_input(p, "Too many registers")
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->tyregs[pcl->nregs] = gssymtable_get_type(symtable, p->label);
        if (!pcl->tyregs[pcl->nregs])
            gsfatal_bad_input(p, "Couldn't find type global %s", p->label->name)
        ;
        pcl->tyregkinds[pcl->nregs] = gssymtable_get_type_expr_kind(symtable, p->label);
        if (!pcl->tyregkinds[pcl->nregs])
            gsfatal_unimpl(__FILE__, __LINE__, "%P: couldn't find kind of '%s'", p->pos, p->label->name)
        ;
        pcl->nregs++;
        return 1;
    } else if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
        if (pcl->regtype > rtcode)
            gsfatal_bad_input(p, "Too late to add sub-expressions")
        ;
        pcl->regtype = rtcode;
        if (pcl->ncodes >= MAX_NUM_REGISTERS)
            gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
        ;
        pcl->coderegs[pcl->ncodes] = p->label;
        pcl->codetypes[pcl->ncodes] = gssymtable_get_code_type(symtable, p->label);
        if (!pcl->codetypes[pcl->ncodes])
            gsfatal_bad_input(p, "Can't find type of sub-expression %s", p->label->name)
        ;
        pcl->ncodes++;
        return 1;
    } else if (
        gssymceq(p->directive, gssymarg, gssymcodeop, ".arg")
        || gssymceq(p->directive, gssymlarg, gssymcodeop, ".larg")
    ) {
        int reg;
        struct gstype *argtype;

        if (pcl->regtype > rtarg)
            gsfatal_bad_input(p, "Too late to add arguments")
        ;
        pcl->regtype = rtarg;
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal_bad_input(p, "Too many registers")
        ;
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "var");
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        argtype = pcl->tyregs[reg];
        if (!argtype)
            gsfatal_bad_input(p, "Register %s is not a type", p->arguments[0]);
        for (i = 1; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            argtype = gstype_apply(p->pos, argtype, pcl->tyregs[regarg]);
        }
        pcl->regtypes[pcl->nregs] = argtype;
        pcl->nregs++;

        pcl->argtypes[pcl->nargs] = argtype;
        pcl->arglines[pcl->nargs] = p;
        pcl->arglifted[pcl->nargs] = p->directive == gssymlarg;
        pcl->nargs++;

        return 1;
    } else {
        return 0;
    }
}

static
struct gstype *
gsbc_typecheck_code_or_api_args(int nargs, struct gstype **argtypes, struct gsparsedline **arglines, int *arglifted, struct gstype *calculated_type)
{
    struct gsparsedline *p;

    while (nargs--) {
        struct gstype *ty;

        ty = argtypes[nargs];
        p = arglines[nargs];

        calculated_type = gstypes_compile_fun(p->pos, ty, calculated_type);
        if (arglifted[nargs])
            calculated_type = gstypes_compile_lift(p->pos, calculated_type)
        ;
    }

    return calculated_type;
}

static char *gsbc_typecheck_compile_prim_type_skip_ws(char *);
static char *gsbc_typecheck_compile_prim_type_skip_token(char *);

struct gsbc_typecheck_compile_prim_type_var {
    gsinterned_string var;
    struct gskind *kind;
};

static
struct gstype *
gsbc_typecheck_compile_prim_type(struct gspos pos, struct gsfile_symtable *symtable, char *s)
{
    char buf[0x200];
    int nvars;
    struct gsbc_typecheck_compile_prim_type_var vars[MAX_NUM_REGISTERS];
    int stacksize;
    struct gstype *stack[MAX_NUM_REGISTERS];
    int i;

    if (strecpy(buf, buf + sizeof(buf), s) >= buf + sizeof(buf))
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Buffer overflow copying type: %s", pos, s)
    ;

    s = gsbc_typecheck_compile_prim_type_skip_ws(buf);
    nvars = 0;
    stacksize = 0;
    while (*s) {
        char *tok;

        tok = s;
        s = gsbc_typecheck_compile_prim_type_skip_token(s);

        if (!strcmp("λ", tok)) {
            if (nvars >= MAX_NUM_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Too many λs in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
            ;

            if (!*s) gsfatal("%P: Can't parse %s: missing type var argument to λ", pos, buf);
            tok = s;
            s = gsbc_typecheck_compile_prim_type_skip_token(s);
            vars[nvars].var = gsintern_string(gssymtypelable, tok);

            if (!*s) gsfatal("%P: Can't parse %s: missing kind argument to λ", pos, buf);
            tok = s;
            s = gsbc_typecheck_compile_prim_type_skip_token(s);
            vars[nvars].kind = gskind_compile(pos, gsintern_string(gssymkindexpr, tok));

            nvars++;
        } else if (!strcmp("\"apiprim", tok)) {
            struct gsregistered_primset *prims;
            struct gsregistered_primtype *primtype;
            gsinterned_string primtypename;
            struct gskind *primtypekind;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Stack overflow in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
            ;

            if (!*s) gsfatal("%P: Can't parse %s: missing primset argument to \"apiprim", pos, buf);
            tok = s;
            s = gsbc_typecheck_compile_prim_type_skip_token(s);
            prims = gsprims_lookup_prim_set(tok);
            if (!prims) gsfatal("%P: Primset %s not declared (in internal primtype)", pos, tok);

            if (!*s) gsfatal("%P: Can't parse %s: missing primtype argument to \"apiprim", pos, buf);
            tok = s;
            s = gsbc_typecheck_compile_prim_type_skip_token(s);
            primtype = gsprims_lookup_type(prims, tok);
            if (!primtype) gsfatal("%P: Primset %s contains no primtype %s", pos, prims->name, tok);

            primtypename = gsintern_string(gssymtypelable, primtype->name);
            primtypekind = gskind_compile(pos, gsintern_string(gssymkindexpr, primtype->kind));
            stack[stacksize++] = gstypes_compile_knprim(pos, gsprim_type_api, prims, primtypename, primtypekind);
        } else if (!strcmp("`", tok)) {
            struct gstype *ty;

            if (stacksize < 2)
                gsfatal("%P: Stack underflow in parsing internal primtype", pos)
            ;

            ty = gstype_apply(pos, stack[stacksize - 2], stack[stacksize - 1]);
            stacksize -= 2;
            stack[stacksize++] = ty;
        } else if (!strcmp("→", tok)) {
            struct gstype *ty;

            if (stacksize < 2)
                gsfatal("%P: Stack underflow in parsing internal primtype", pos)
            ;

            ty = gstypes_compile_fun(pos, stack[stacksize - 2], stack[stacksize - 1]);
            stacksize -= 2;
            stack[stacksize++] = ty;
        } else if (!strcmp("∀", tok)) {
            struct gstype *ty;

            if (stacksize < 1)
                gsfatal("%P: Stack underflow in parsing internal primtype", pos)
            ;

            if (nvars < 1)
                gsfatal("%P: Not enough λs in internal primtype", pos)
            ;

            ty = gstypes_compile_forall(pos, vars[nvars - 1].var, vars[nvars - 1].kind, stack[stacksize - 1]);
            nvars--;
            stacksize--;

            stack[stacksize++] = ty;
        } else if (!strcmp("〈", tok)) {
            int nfields;
            struct gstype_field fields[MAX_NUM_REGISTERS];

            nfields = 0;
            while (tok = s, s = gsbc_typecheck_compile_prim_type_skip_token(s), strcmp("〉", tok)) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Get fields in internal primtype next", pos);
            }
            if (nfields > MAX_NUM_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Too many fields in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
            ;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Stack overflow in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
            ;

            stack[stacksize++] = gstypes_compile_productv(pos, nfields, fields);
        } else if (!strcmp("⌊⌋", tok)) {
            struct gstype *type;

            if (stacksize < 1)
                gsfatal("%P: Stack underflow in parsing internal primtype", pos)
            ;

            type = gstypes_compile_lift(pos, stack[stacksize - 1]);
            stacksize--;

            stack[stacksize++] = type;
        } else {
            gsinterned_string var;
            struct gstype *ty;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Stack overflow in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
            ;

            ty = 0;
            var = gsintern_string(gssymtypelable, tok);
            for (i = nvars; i--; ) {
                if (vars[i].var == var) {
                    ty = gstypes_compile_type_var(pos, var, vars[i].kind);
                    break;
                }
            }
            if (!ty)
                ty = gssymtable_get_type(symtable, var)
            ;
            if (!ty)
                gsfatal("%P: Cannot find type variable %y (in internal primtype)", pos, var)
            ;

            stack[stacksize++] = ty;
        }
    }

    if (nvars > 0) gsfatal("%P: Too many λs in internal primtype");

    if (stacksize < 0) gsfatal("%P: Stack underflow at end of internal primtype", pos);
    if (stacksize > 1) gsfatal("%P: Stack overflow at end of internal primtype", pos);

    return stack[0];
}

static
char *
gsbc_typecheck_compile_prim_type_skip_ws(char *s)
{
    return s;
}

static
char *
gsbc_typecheck_compile_prim_type_skip_token(char *s)
{
    while (*s && !isspace(*s)) s++;

    if (!*s)
        return s
    ;

    *s = 0;

    return gsbc_typecheck_compile_prim_type_skip_ws(s + 1);
}

static
int
gsbc_typecheck_free_type_variables(struct gsparsedline *p, struct gsbc_code_item_type *cty)
{
    int i;
    int nftyvs;

    for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_api_expr(.body type free variables)", p->pos);
    }
    nftyvs = i - 1;
    if (nftyvs < cty->numftyvs)
        gsfatal_bad_input(p, "Not enough free type variables for %s; need %d but have %d", p->arguments[0]->name, cty->numftyvs, nftyvs)
    ;
    if (nftyvs > cty->numftyvs)
        gsfatal_bad_input(p, "Too many free type variables for %s; only need %d but have %d", p->arguments[0]->name, cty->numftyvs, nftyvs)
    ;
    return nftyvs;
}

static
int
gsbc_typecheck_free_variables(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gsbc_code_item_type *cty, int nftyvs)
{
    int i;
    int nfvs;
    int firstfv;

    i = 1 + nftyvs;
    if (i < p->numarguments) i++;
    firstfv = i;
    nfvs = p->numarguments - i;
    if (nfvs < cty->numfvs)
        gsfatal_bad_input(p, "Not enough free variables for %s; need %d but have %d", p->arguments[0]->name, cty->numfvs, nfvs)
    ;
    if (nfvs > cty->numfvs)
        gsfatal_bad_input(p, "Too many free variables for %s; only need %d but have %d", p->arguments[0]->name, cty->numfvs, nfvs)
    ;
    for (; i < p->numarguments; i++) {
        int fvreg;
        struct gstype *fvtype;

        fvreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
        fvtype = pcl->regtypes[fvreg];
        if (nftyvs > 0)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_api_expr: substitute free type variables in expected type of free variable", p->pos)
        ;
        gstypes_type_check_type_fail(p->pos, fvtype, cty->fvtypes[i - firstfv]);
    }
    return nfvs;
}

static
struct gstype *
gsbc_typecheck_check_api_statement_type(struct gspos pos, struct gstype *ty, gsinterned_string primsetname, gsinterned_string primname, int *plifted)
{
    char buf[0x100];
    struct gstype *res;

    if (gstypes_eprint_type(buf, buf + sizeof(buf), ty) >= buf + sizeof(buf))
        gsfatal("%s:%d: %P: buffer overflow printing type %P", __FILE__, __LINE__, pos, ty->pos)
    ;

    if (ty->node == gstype_lift) {
        struct gstype_lift *lift;

        lift = (struct gstype_lift *)ty;
        ty = lift->arg;

        if (plifted)
            *plifted = 1
        ;
    }

    res = 0;

    if (ty->node != gstype_app) {
        gsfatal("%P: I don't think %s is a type application", pos, buf);
    } else {
        struct gstype_app *app;

        app = (struct gstype_app *)ty;
        res = app->arg;

        ty = app->fun;
        while (ty->node == gstype_app) {
            app = (struct gstype_app *)ty;
            ty = app->fun;
        }
    }

    if (ty->node == gstype_unprim) {
        struct gstype_unprim *prim;

        prim = (struct gstype_unprim *)ty;

        if (prim->primtypegroup != gsprim_type_api)
            gsfatal("%P: I don't think %s is an API primitive type", pos, buf)
        ;
        if (prim->primsetname != primsetname)
            gsfatal("%P: I don't think %s is a type in the %s primset", pos, buf, primsetname->name)
        ;
        if (prim->primname != primname)
            gsfatal("%P: I don't think %s is the primtype %s %s", pos, buf, primsetname->name, primname->name)
        ;
    } else if (ty->node == gstype_knprim) {
        struct gstype_knprim *prim;

        prim = (struct gstype_knprim *)ty;

        if (prim->primtypegroup != gsprim_type_api)
            gsfatal("%P: I don't think %s is an API primitive type", pos, buf)
        ;
        if (strcmp(prim->primset->name, primsetname->name))
            gsfatal("%P: I don't think %s is a type in the %s primset", pos, buf, primsetname->name)
        ;
        if (prim->primname != primname)
            gsfatal("%P: I don't think %s is the primtype %s %s", pos, buf, primsetname->name, primname->name)
        ;
    } else {
        gsfatal("%P: I don't think %s is an application of a primitive", pos, buf);
    }

    return res;
}

static
void
gstypes_type_check_type_fail(struct gspos pos, struct gstype *pactual, struct gstype *pexpected)
{
    char err[0x100];

    if (gstypes_type_check(err, err + sizeof(err), pos, pactual, pexpected) < 0)
        gsfatal("%s", err)
    ;
}

int
gstypes_type_check(char *err, char *eerr, struct gspos pos, struct gstype *pactual, struct gstype *pexpected)
{
    char actual_buf[0x100];
    char expected_buf[0x100];

    if (gstypes_eprint_type(actual_buf, actual_buf + sizeof(actual_buf), pactual) >= actual_buf + sizeof(actual_buf)) {
        seprint(err, eerr, "%s:%d: %P: buffer overflow printing actual type %P", __FILE__, __LINE__, pos, pactual->pos);
        return -1;
    }
    if (gstypes_eprint_type(expected_buf, expected_buf + sizeof(expected_buf), pexpected) >= actual_buf + sizeof(actual_buf)) {
        seprint(err, eerr, "%s:%d: %P: buffer overflow printing actual type %P", __FILE__, __LINE__, pos, pactual->pos);
        return -1;
    }

    if (pactual->node != pexpected->node) {
        seprint(err, eerr, "%P: I don't think %s (%P) is %s (%P)", pos, actual_buf, pactual->pos, expected_buf, pexpected->pos);
        return -1;
    }

    switch (pexpected->node) {
        case gstype_abstract: {
            struct gstype_abstract *pactual_abstract, *pexpected_abstract;

            pactual_abstract = (struct gstype_abstract *)pactual;
            pexpected_abstract = (struct gstype_abstract *)pexpected;

            if (pactual_abstract->name != pexpected_abstract->name) {
                seprint(err, eerr, "%P: I don't think abstype %y is the same as abstype %y", pos, pactual_abstract->name, pexpected_abstract->name);
                return -1;
            }
            return 0;
        }
        case gstype_knprim: {
            struct gstype_knprim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_knprim *)pactual;
            pexpected_prim = (struct gstype_knprim *)pexpected;

            if (pactual_prim->primset != pexpected_prim->primset) {
                seprint(err, eerr, "%P: I don't think primset %s is the same as primset %s", pos, pactual_prim->primset->name, pexpected_prim->primset->name);
                return -1;
            }
            if (pactual_prim->primname != pexpected_prim->primname) {
                seprint(err, eerr, "%P: I don't think prim %s is the same as prim %s", pos, pactual_prim->primname->name, pexpected_prim->primname->name);
                return -1;
            }
            return 0;
        }
        case gstype_unprim: {
            struct gstype_unprim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_unprim *)pactual;
            pexpected_prim = (struct gstype_unprim *)pexpected;

            if (pactual_prim->primsetname != pexpected_prim->primsetname) {
                seprint(err, eerr, "%P: I don't think primset %s is the same as primset %s", pos, pactual_prim->primsetname->name, pexpected_prim->primsetname->name);
                return -1;
            }
            if (pactual_prim->primname != pexpected_prim->primname) {
                seprint(err, eerr, "%P: I don't think prim %s is the same as prim %s", pos, pactual_prim->primname->name, pexpected_prim->primname->name);
                return -1;
            }
            return 0;
        }
        case gstype_var: {
            struct gstype_var *pactual_var, *pexpected_var;

            pactual_var = (struct gstype_var *)pactual;
            pexpected_var = (struct gstype_var *)pexpected;

            if (pactual_var->name != pexpected_var->name) {
                seprint(err, eerr, "%P: I don't think %s is the same as %s", pos, pactual_var->name, pexpected_var->name);
                return -1;
            }
            return 0;
        }
        case gstype_forall: {
            struct gstype_forall *pactual_forall, *pexpected_forall;
            struct gstype *actual_renamed_body, *expected_renamed_body;
            char nm[0x100];
            int n;
            gsinterned_string var;
            struct gstype *varty;

            pactual_forall = (struct gstype_forall *)pactual;
            pexpected_forall = (struct gstype_forall *)pexpected;

            if (gstypes_kind_check(pos, pactual_forall->kind, pexpected_forall->kind, err, eerr) < 0)
                return -1
            ;

            n = 0;
            do {
                if (seprint(nm, nm + sizeof(nm), "%y%d", pexpected_forall->var, n++) >= nm + sizeof(nm)) {
                    seprint(err, eerr, "%s:%d: %P: Buffer overflow during α-renaming", __FILE__, __LINE__, pexpected->pos);
                    return -1;
                }
                var = gsintern_string(gssymtypelable, nm);
            } while (gstypes_is_ftyvar(var, pactual_forall->body) || gstypes_is_ftyvar(var, pexpected_forall->body));
            varty = gstypes_compile_type_var(pos, var, pactual_forall->kind);
            actual_renamed_body = gstypes_subst(pos, pactual_forall->body, pactual_forall->var, varty);
            expected_renamed_body = gstypes_subst(pos, pexpected_forall->body, pexpected_forall->var, varty);

            return gstypes_type_check(err, eerr, pos, actual_renamed_body, expected_renamed_body);
        }
        case gstype_lift: {
            struct gstype_lift *pactual_lift, *pexpected_lift;

            pactual_lift = (struct gstype_lift *)pactual;
            pexpected_lift = (struct gstype_lift *)pexpected;

            return gstypes_type_check(err, eerr, pos, pactual_lift->arg, pexpected_lift->arg);
        }
        case gstype_app: {
            struct gstype_app *pactual_app, *pexpected_app;

            pactual_app = (struct gstype_app *)pactual;
            pexpected_app = (struct gstype_app *)pexpected;

            if (gstypes_type_check(err, eerr, pos, pactual_app->fun, pexpected_app->fun) < 0)
                return -1;
            if (gstypes_type_check(err, eerr, pos, pactual_app->arg, pexpected_app->arg) < 0)
                return -1;
            return 0;
        }
        case gstype_fun: {
            struct gstype_fun *pactual_fun, *pexpected_fun;

            pactual_fun = (struct gstype_fun *)pactual;
            pexpected_fun = (struct gstype_fun *)pexpected;

            if (gstypes_type_check(err, eerr, pos, pactual_fun->tyarg, pexpected_fun->tyarg) < 0)
                return -1;
            if (gstypes_type_check(err, eerr, pos, pactual_fun->tyres, pexpected_fun->tyres) < 0)
                return -1;
            return 0;
        }
        case gstype_sum: {
            struct gstype_sum *pactual_sum, *pexpected_sum;

            pactual_sum = (struct gstype_sum *)pactual;
            pexpected_sum = (struct gstype_sum *)pexpected;

            if (pactual_sum->numconstrs != pexpected_sum->numconstrs) {
                seprint(err, eerr, "%s:%d: I don't think %s is the same as %s; they have diferent numbers of constrs", pos.file->name, pos.lineno, actual_buf, expected_buf);
                return -1;
            }
            if (pexpected_sum->numconstrs > 0) {
                seprint(err, eerr, "%s:%d: %P: %P: gstypes_check_type(check constrs)", __FILE__, __LINE__, pos, pexpected->pos);
                return -1;
            }
            return 0;
        }
        case gstype_product: {
            struct gstype_product *pactual_product, *pexpected_product;

            pactual_product = (struct gstype_product *)pactual;
            pexpected_product = (struct gstype_product *)pexpected;

            if (pactual_product->numfields != pexpected_product->numfields) {
                seprint(err, eerr, "%s:%d: I don't think %s is the same as %s; they have diferent numbers of fields", pos.file->name, pos.lineno, actual_buf, expected_buf);
                return -1;
            }
            if (pexpected_product->numfields > 0) {
                seprint(err, eerr, "%s:%d: %P: %P: gstypes_check_type(check fields)", __FILE__, __LINE__, pos, pexpected->pos);
                return -1;
            }
            return 0;
        }
        default:
            seprint(err, eerr, "%s:%d: gstypes_check_type(node = %d) %P: %P", __FILE__, __LINE__, pexpected->node, pos, pexpected->pos);
            return -1;
    }
}

static char *gstypes_eprint_kind(char *, char *, struct gskind *, int);

char *
gstypes_eprint_type(char *res, char *eob, struct gstype *pty)
{
    int i;

    switch (pty->node) {
        case gstype_abstract: {
            struct gstype_abstract *abstract;

            abstract = (struct gstype_abstract *)pty;

            res = seprint(res, eob, "%y", abstract->name);
            return res;
        }
        case gstype_knprim: {
            struct gstype_knprim *prim;

            prim = (struct gstype_knprim *)pty;

            switch (prim->primtypegroup) {
                case gsprim_type_defined:
                    res = seprint(res, eob, "\"definedprim ");
                    break;
                case gsprim_type_api:
                    res = seprint(res, eob, "\"apiprim ");
                    break;
                default:
                    res = seprint(res, eob, "%s:%d: %d primitives next ", __FILE__, __LINE__, prim->primtypegroup);
                    break;
            }
            res = seprint(res, eob, "%s ", prim->primset->name);
            res = seprint(res, eob, "%s", prim->primname->name);
            return res;
        }
        case gstype_unprim: {
            struct gstype_unprim *prim;

            prim = (struct gstype_unprim *)pty;

            switch (prim->primtypegroup) {
                case gsprim_type_elim:
                    res = seprint(res, eob, "\"elimprim ");
                    break;
                case gsprim_type_api:
                    res = seprint(res, eob, "\"apiprim ");
                    break;
                default:
                    res = seprint(res, eob, "%s:%d: %d primitives next ", __FILE__, __LINE__, prim->primtypegroup);
                    break;
            }
            res = seprint(res, eob, "%s ", prim->primsetname->name);
            res = seprint(res, eob, "%s", prim->primname->name);
            return res;
        }
        case gstype_var: {
            struct gstype_var *var;

            var = (struct gstype_var *)pty;

            res = seprint(res, eob, "%s", var->name->name);
            return res;
        }
        case gstype_forall: {
            struct gstype_forall *forall;

            forall = (struct gstype_forall *)pty;

            res = seprint(res, eob, "(∀ %s :: ", forall->var->name);
            res = gstypes_eprint_kind(res, eob, forall->kind, 0);
            res = seprint(res, eob, ". ");
            res = gstypes_eprint_type(res, eob, forall->body);
            res = seprint(res, eob, ")");
            return res;
        }
        case gstype_lift: {
            struct gstype_lift *lift;

            lift = (struct gstype_lift *)pty;

            res = seprint(res, eob, "⌊");
            res = gstypes_eprint_type(res, eob, lift->arg);
            res = seprint(res, eob, "⌋");
            return res;
        }
        case gstype_app: {
            struct gstype_app *app;

            app = (struct gstype_app *)pty;

            res = gstypes_eprint_type(res, eob, app->fun);
            res = seprint(res, eob, " (");
            res = gstypes_eprint_type(res, eob, app->arg);
            res = seprint(res, eob, ")");
            return res;
        }
        case gstype_fun: {
            struct gstype_fun *fun;

            fun = (struct gstype_fun *)pty;

            res = seprint(res, eob, "(");
            res = gstypes_eprint_type(res, eob, fun->tyarg);
            res = seprint(res, eob, ") → ");
            res = gstypes_eprint_type(res, eob, fun->tyres);
            return res;
        }
        case gstype_sum: {
            struct gstype_sum *sum;

            sum = (struct gstype_sum *)pty;

            res = seprint(res, eob, "\"Σ〈");
            for (i = 0; i < sum->numconstrs; i++) {
                res = seprint(res, eob, "%s:%d: print constructors next", __FILE__, __LINE__);
            }
            res = seprint(res, eob, "〉");
            return res;
        }
        case gstype_product: {
            struct gstype_product *product;

            product = (struct gstype_product *)pty;

            res = seprint(res, eob, "\"Π〈");
            for (i = 0; i < product->numfields; i++) {
                res = seprint(res, eob, "%s:%d: print fields next", __FILE__, __LINE__);
            }
            res = seprint(res, eob, "〉");
            return res;
        }
        default:
            res = seprint(res, eob, "%s:%d: unknown type node %d", __FILE__, __LINE__, pty->node);
            return res;
    }
}

static
char *
gstypes_eprint_kind(char *res, char *eob, struct gskind *ky, int parens)
{
    switch (ky->node) {
        case gskind_lifted:
            res = seprint(res, eob, "*");
            return res;
        default:
            if (parens)
                res = seprint(res, eob, "(")
            ;
            res = seprint(res, eob, "%s:%d: unknown kind node %d", __FILE__, __LINE__, ky->node);
            if (parens)
                res = seprint(res, eob, ")")
            ;
            return res;
    }
}

static struct gsbc_coercion_type *gsbc_typecheck_coercion_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p);

static
void
gstypes_type_check_coercion_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    struct gsparsedline *pcoercion;
    struct gsparsedfile_segment *pseg;

    pseg = items[i].pseg;
    pcoercion = items[i].v;
    if (gssymeq(pcoercion->directive, gssymcoerciondirective, ".tycoercion")) {
        struct gsbc_coercion_type *type;

        type = gsbc_typecheck_coercion_expr(symtable, &pseg, gsinput_next_line(&pseg, pcoercion));
        gssymtable_set_coercion_type(symtable, pcoercion->label, type);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_type_check_coercion_item(%s)", pcoercion->pos, pcoercion->directive->name);
    }
}

static struct gs_block_class gsbc_coercion_type_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Coercion types",
};
void *gsbc_coercion_type_nursury;

static
struct gsbc_coercion_type *
gsbc_typecheck_coercion_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    enum {
        rttygvar,
        rttyarg,
        rtcont,
    } regtype;
    int i;
    int nregs, nglobals, nargs, nconts;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    struct gstype *regtypes[MAX_NUM_REGISTERS];
    struct gstype *globaldefns[MAX_NUM_REGISTERS];
    struct gskind *argkinds[MAX_NUM_REGISTERS];
    struct gsparsedline *arglines[MAX_NUM_REGISTERS];
    struct gsparsedline *conts[MAX_NUM_REGISTERS];
    struct gsbc_coercion_type *res;
    struct gstype *dest, *source;

    res = 0;

    nregs = nglobals = nargs = nconts = 0;
    regtype = rttygvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcoercionop, ".tygvar")) {
            if (regtype > rttygvar)
                gsfatal_bad_input(p, "Too late to add type global variables");
            regtype = rttygvar;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            regs[nregs] = p->label;
            regtypes[nregs] = gssymtable_get_type(symtable, p->label);
            if (!regtypes[nregs])
                gsfatal_bad_input(p, "Couldn't find type for global %s", p->label->name)
            ;
            globaldefns[nglobals] = gssymtable_get_abstype(symtable, p->label);
            nregs++;
            nglobals++;
        } else if (gssymeq(p->directive, gssymcoercionop, ".tylambda")) {
            struct gskind *kind;

            if (regtype > rttyarg)
                gsfatal_bad_input(p, "Too late to add type arguments")
            ;
            regtype = rttyarg;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "kind");
            kind = gskind_compile(p->pos, p->arguments[0]);
            regtypes[nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
            argkinds[nargs] = kind;
            arglines[nargs] = p;
            nregs++;
            nargs++;
        } else if (gssymeq(p->directive, gssymcoercionop, ".tyinvert")) {
            regtype = rtcont;
            if (nconts >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many continuations")
            ;
            conts[nconts] = p;
            nconts++;
        } else if (gssymeq(p->directive, gssymcoercionop, ".tydefinition")) {
            int reg, global;

            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            if (reg >= nglobals)
                gsfatal_bad_input(p, "Register %s isn't a global; can't really cast to/from definition of an abstract type unless we know what that definition is", p->arguments[0]->name)
            ;
            global = reg;
            if (!regtypes[reg])
                gsfatal_bad_input(p, "Register %s doesn't seem to be a type variable", p->arguments[0]->name);
            dest = regtypes[global];

            if (!globaldefns[global])
                gsfatal_bad_input(p, "Register %s doesn't seem to be an abstract type", p->arguments[0]->name);
            source = globaldefns[global];

            for (i = 1; i < p->numarguments; i++) {
                reg = gsbc_find_register(p, regs, nregs, p->arguments[i]);
                if (!regtypes[reg])
                    gsfatal_bad_input(p, "Register %s doesn't seem to be a type variable", p->arguments[i]->name)
                ;
                source = gstype_supply(p->pos, source, regtypes[reg]);
                dest = gstype_apply(p->pos, dest, regtypes[reg]);
            }

            if (source->node == gstype_lambda)
                gsfatal_bad_input(p, "Not enough arguments to definition of %s", p->arguments[0]->name)
            ;

            goto have_type;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_coercion_expr(%s)", p->pos, p->directive->name);
        }
    }

have_type:

    while (nconts--) {
        p = conts[nconts];

        if (gssymeq(p->directive, gssymcoercionop, ".tyinvert")) {
            struct gstype *tmp;

            tmp = source;
            source = dest;
            dest = tmp;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_coercion_expr(cont %s)", p->pos, p->directive->name);
        }
    }

    while (nargs--) {
        gsinterned_string var;
        struct gskind *kind;

        var = regs[nglobals + nargs];
        kind = argkinds[nargs];

        source = gstypes_compile_lambda(arglines[nargs]->pos, var, kind, source);
        dest = gstypes_compile_lambda(arglines[nargs]->pos, var, kind, dest);
    }

    res = gs_sys_seg_suballoc(&gsbc_coercion_type_descr, &gsbc_coercion_type_nursury, sizeof(*res), sizeof(void*));
    res->source = source;
    res->dest = dest;

    return res;
}
