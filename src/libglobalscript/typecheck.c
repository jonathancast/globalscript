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
    static gsinterned_string gssymtyabstract, gssymtyexpr, gssymtydefinedprim, gssymtyapiprim, gssymtyintrprim, gssymtyelimprim;

    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedline *ptype;

                    ptype = item.v;
                    if (gssymceq(ptype->directive, gssymtyabstract, gssymtypedirective, ".tyabstract")) {
                        gsargcheck(ptype, 0, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[0]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymceq(ptype->directive, gssymtyexpr, gssymtypedirective, ".tyexpr")) {
                        if (ptype->numarguments > 0) {
                            kinds[i] = gskind_compile(ptype->pos, ptype->arguments[0]);

                            gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                        } else {
                            kinds[i] = 0;
                        }
                    } else if (gssymceq(ptype->directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymceq(ptype->directive, gssymtyapiprim, gssymtypedirective, ".tyapiprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymceq(ptype->directive, gssymtyintrprim, gssymtypedirective, ".tyintrprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype->pos, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else {
                        gsfatal(UNIMPL("%P: gstypes_process_type_declarations(%y)"), ptype->pos, ptype->directive);
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

static void gstypes_kind_check_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gstype **, struct gskind **, int, int);

void
gstypes_kind_check_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gstype **defns, struct gskind **kinds, int n)
{
    int i;

    for (i = 0; i < n; i++)
        gstypes_kind_check_item(symtable, items, types, defns, kinds, n, i)
    ;
}

static
void
gstypes_kind_check_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gstype **defns, struct gskind **kinds, int n, int i)
{
    switch (items[i].type) {
        case gssymtypelable: {
            struct gstype *type, *defn;
            struct gskind *calculated_kind;

            type = types[i];
            defn = defns[i];

            calculated_kind = gstypes_calculate_kind(type);

            if (defn) {
                struct gskind *defn_kind;

                defn_kind = gstypes_calculate_kind(defn);
                gstypes_kind_check_fail(type->pos, defn_kind, calculated_kind);
            }

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
            gsfatal(UNIMPL("%P: gstypes_kind_check_item(type = %d)"), items[i].v->pos, items[i].type);
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
            if (abs->kind) return abs->kind;
            gsfatal(UNIMPL("%P: Abstract types without declared kinds"), type->pos);
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
                gsfatal(UNIMPL("Panic! Primitype type %s (%s:%d) lacks a declared kind"), primtype->name, primtype->file, primtype->line)
            ;
            pos.file = gsintern_string(gssymfilename, primtype->file);
            pos.lineno = primtype->line;
            return gskind_compile(pos, gsintern_string(gssymkindexpr, primtype->kind));
        }
        case gstype_unprim: {
            struct gstype_unprim *prim;

            prim = (struct gstype_unprim *)type;
            if (!prim->kind)
                gsfatal("%P: Primitive %y seems to lack a declared kind", type->pos, prim->primname);
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
        case gstype_exists: {
            struct gstype_exists *exists;
            struct gskind *kybody;

            exists = (struct gstype_exists *)type;

            kybody = gstypes_calculate_kind(exists->body);
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
        case gstype_ubsum: {
            struct gstype_ubsum *sum;

            sum = (struct gstype_ubsum *)type;

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
                struct gskind *kind;

                kind = gstypes_calculate_kind(prod->fields[i].type);
                gstypes_kind_check_simple(type->pos, kind);
                gsbc_typecheck_check_boxed(type->pos, prod->fields[i].type);
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
            gsfatal(UNIMPL("%P: gstypes_calculate_kind(node = %d)"), type->pos, type->node);
    }
    return 0;
}

void
gstypes_kind_check_fail(struct gspos pos, struct gskind *kyactual, struct gskind *kyexpected)
{
    struct gsstringbuilder *err;

    err = gsreserve_string_builder();
    if (gstypes_kind_check(pos, kyactual, kyexpected, err) < 0) {
        gsfinish_string_builder(err);
        gsfatal("%s", err->start);
    }
    gsfinish_string_builder(err);
}

static char *seprint_kind_name(char *, char *, struct gskind *);

int
gstypes_kind_check(struct gspos pos, struct gskind *kyactual, struct gskind *kyexpected, struct gsstringbuilder *err)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

    switch (kyexpected->node) {
        case gskind_unknown:
            if (kyactual->node != gskind_unknown && kyactual->node != gskind_unlifted && kyactual->node != gskind_lifted) {
                gsstring_builder_print(err, "%P: Incorrect kind: Expected '?'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_unlifted:
            if (kyactual->node != gskind_unlifted) {
                gsstring_builder_print(err, "%P: Incorrect kind: Expected 'u'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_lifted:
            if (kyactual->node != gskind_lifted) {
                gsstring_builder_print(err, "%P: Incorrect kind: Expected '*'; got '%s'", pos, actual_name);
                return -1;
            }
            return 0;
        case gskind_exponential:
            if (kyactual->node != gskind_exponential) {
                gsstring_builder_print(err, "%P: Incorrect kind: Expected '^'; got '%s'", pos, actual_name);
                return -1;
            }
            if (gstypes_kind_check(pos, kyactual->args[0], kyexpected->args[0], err) < 0)
                return -1
            ;
            if (gstypes_kind_check(pos, kyactual->args[1], kyexpected->args[1], err) < 0)
                return -1
            ;
            return 0;
        default:
            gsstring_builder_print(err, UNIMPL("%P: gstypes_kind_check(expected = %d)"), pos, kyexpected->node);
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
        case gskind_unknown:
        case gskind_unlifted:
        case gskind_lifted:
            return;
        case gskind_exponential:
            gsfatal("%P: Not enough arguments; kind is %s", pos, actual_name);
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
        case gstype_abstract:
        case gstype_knprim:
        case gstype_unprim:
        case gstype_var:
        case gstype_lift:
        case gstype_app:
        case gstype_fun:
        case gstype_sum:
        case gstype_product:
            return;
        case gstype_forall: {
            struct gstype_forall *forall;
            forall = (struct gstype_forall *)type;
            return gsbc_typecheck_check_boxed(pos, forall->body);
        }
        case gstype_exists: {
            struct gstype_exists *exists;
            exists = (struct gstype_exists *)type;
            return gsbc_typecheck_check_boxed(pos, exists->body);
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda;
            lambda = (struct gstype_lambda *)type;
            return gsbc_typecheck_check_boxed(pos, lambda->body);
        }
        case gstype_ubproduct:
            gsfatal("%P: Expected boxed type but got un-boxed product type", pos);
        default:
            gsfatal(UNIMPL("%P: %P: gsbc_typecheck_check_boxed(node = %d)"), pos, type->pos, type->node);
    }
}

void
gsbc_typecheck_check_boxed_or_product(struct gspos pos, struct gstype *type)
{
    switch (type->node) {
        case gstype_knprim:
        case gstype_unprim:
        case gstype_var:
        case gstype_lift:
        case gstype_app:
        case gstype_fun:
        case gstype_ubproduct:
            return;
        case gstype_forall: {
            struct gstype_forall *forall;
            forall = (struct gstype_forall *)type;
            return gsbc_typecheck_check_boxed_or_product(pos, forall->body);
        }
        case gstype_exists: {
            struct gstype_exists *exists;
            exists = (struct gstype_exists *)type;
            return gsbc_typecheck_check_boxed_or_product(pos, exists->body);
        }
        case gstype_ubsum:
            gsfatal("%P: Invalid un-boxed sum type", pos);
        default:
            gsfatal(UNIMPL("%P: %P: gsbc_typecheck_check_boxed_or_product(node = %d)"), pos, type->pos, type->node);
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
    static gsinterned_string gssymrecord, gssymconstr, gssymrune, gssymstring, gssymlist, gssymregex, gssymundefined, gssymclosure, gssymcast;

    struct gsparsedline *pdata;

    pdata = item.v;

    if (gssymceq(pdata->directive, gssymrecord, gssymdatadirective, ".record")) {
        return;
    } else if (
        gssymceq(pdata->directive, gssymrune, gssymdatadirective, ".rune")
        || gssymceq(pdata->directive, gssymstring, gssymdatadirective, ".string")
    ) {
        return;
    } else if (gssymceq(pdata->directive, gssymlist, gssymdatadirective, ".list")) {
        static gsinterned_string gssymtylist;
        struct gstype *list, *elem_type;

        if (!gssymtylist)
            gssymtylist = gsintern_string(gssymtypelable, "list.t")
        ;
        list = gssymtable_get_type(symtable, gssymtylist);
        elem_type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!elem_type)
            gsfatal("%P: Couldn't find type %y", pdata->pos, pdata->arguments[0])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, gstype_apply(pdata->pos, list, elem_type))
        ; else if (pentrytype)
            *pentrytype = gstype_apply(pdata->pos, list, elem_type)
        ;
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
    } else if (gssymceq(pdata->directive, gssymregex, gssymdatadirective, ".regex")) {
        static gsinterned_string gssymtyregex, gssymtyrune;
        struct gstype *regex, *rune;

        if (!gssymtyregex) gssymtyregex = gsintern_string(gssymtypelable, "regex.t");
        if (!gssymtyrune) gssymtyrune = gsintern_string(gssymtypelable, "rune.t");

        regex = gssymtable_get_type(symtable, gssymtyregex);
        rune = gssymtable_get_type(symtable, gssymtyrune);
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, gstype_apply(pdata->pos, regex, rune))
        ; else if (pentrytype)
            *pentrytype = gstype_apply(pdata->pos, regex, rune)
        ;
    } else if (gssymceq(pdata->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gstype *type;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal("%P: Couldn't find type %y", pdata->pos, pdata->arguments[0])
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
            gsfatal("%P: Couldn't find type %y", pdata->pos, pdata->arguments[1])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
        ; else if (pentrytype)
            *pentrytype = type
        ;
    } else if (gssymceq(pdata->directive, gssymcast, gssymdatadirective, ".cast")) {
        return;
    } else {
        gsfatal(UNIMPL("%P: gstypes_process_data_type_signature(%y)"), item.v->pos, pdata->directive);
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
static void gsbc_check_field_order(struct gspos, int, struct gstype_field *);

static
void
gstypes_type_check_data_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, struct gstype **pentrytype, int n, int i)
{
    static gsinterned_string gssymrecord, gssymconstr, gssymrune, gssymstring, gssymlist, gssymregex, gssymundefined, gssymclosure, gssymcast;

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
        gsargcheck(pdata, 1, "constructor");
        constrnum = gstypes_find_constr_in_sum(pdata->pos, pdata->arguments[0], sum, pdata->arguments[1]);
        argtype = sum->constrs[constrnum].argtype;

        if (pdata->numarguments == 3) {
            struct gstype *actualtype;

            actualtype = gssymtable_get_data_type(symtable, pdata->arguments[2]);
            gstypes_type_check_type_fail(pdata->pos, actualtype, argtype);
        } else {
            struct gstype_ubproduct *ubproduct;

            if (argtype->node != gstype_ubproduct)
                gsfatal("%P: Type of the argument to %y in %y not an un-boxed product, therefore you cannot supply multiple arguments to %y",
                    pdata->pos,
                    pdata->arguments[1],
                    pdata->arguments[0],
                    pdata->arguments[1]
                )
            ;
            ubproduct = (struct gstype_ubproduct *)argtype;
            gsbc_check_field_order(pdata->pos, ubproduct->numfields, ubproduct->fields);
            if ((pdata->numarguments - 2) / 2 != ubproduct->numfields)
                gsfatal("%P: Wrong number of fields; got %d but expected %d", pdata->pos, (pdata->numarguments - 2) / 2, ubproduct->numfields)
            ;
            for (i = 0; 2+i < pdata->numarguments; i += 2) {
                struct gstype *fieldtype;
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
    } else if (gssymceq(pdata->directive, gssymlist, gssymdatadirective, ".list")) {
        struct gstype *elem_type;

        elem_type = gssymtable_get_type(symtable, pdata->arguments[0]);
        for (i = 1; i < pdata->numarguments && pdata->arguments[i]->type != gssymseparator; i++) {
            struct gstype *actual_type = gssymtable_get_data_type(symtable, pdata->arguments[i]);
            gstypes_type_check_type_fail(pdata->pos, actual_type, elem_type);
        }
        if (i < pdata->numarguments)
            gsfatal(UNIMPL("%P: Dotted .lists"), pdata->pos)
        ;
    } else if (gssymceq(pdata->directive, gssymregex, gssymdatadirective, ".regex")) {
        static gsinterned_string gssymtyregex, gssymtyrune;
        struct gstype *regex, *rune, *rerune;

        if (!gssymtyregex) gssymtyregex = gsintern_string(gssymtypelable, "regex.t");
        regex = gssymtable_get_type(symtable, gssymtyregex);

        if (!gssymtyrune) gssymtyrune = gsintern_string(gssymtypelable, "rune.t");
        rune = gssymtable_get_type(symtable, gssymtyrune);

        rerune = gstype_apply(pdata->pos, regex, rune);
        for (i = 1; i < pdata->numarguments; i++) {
            struct gstype *ty;

            ty = gssymtable_get_data_type(symtable, pdata->arguments[i]);
            if (!ty) gsfatal("%P: Cannot find type of %y", pdata->pos, pdata->arguments[i]);
            gstypes_type_check_type_fail(pdata->pos, ty, rerune);
        }
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
            gsfatal("%P: Code for a global data item cannot have free type variables", pdata->pos)
        ;
        if (code_type->numfvs)
            gsfatal("%P: Code for a global data item cannot have free variables", pdata->pos)
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

        src_type = gssymtable_get_data_type(symtable, pdata->arguments[0]);
        if (!src_type) gsfatal(UNIMPL("%P: Couldn't find type of %y"), pdata->pos, pdata->arguments[0]);

        coercion_type = gssymtable_get_coercion_type(symtable, pdata->arguments[1]);
        if (!coercion_type)
            gsfatal("%P: Couldn't find type of coercion %y", pdata->pos, pdata->arguments[1])
        ;

        gstypes_type_check_type_fail(pdata->pos, src_type, coercion_type->source);

        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, coercion_type->dest)
        ; else if (pentrytype)
            *pentrytype = coercion_type->dest
        ;
    } else {
        gsfatal(UNIMPL("%P: gstypes_type_check_data_item(%y)"), pdata->pos, pdata->directive);
    }
}

static struct gsbc_code_item_type *gsbc_typecheck_code_expr(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *);
static struct gsbc_code_item_type *gsbc_typecheck_force_cont(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *);
static struct gsbc_code_item_type *gsbc_typecheck_strict_cont(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *);
static struct gsbc_code_item_type *gsbc_typecheck_ubcase_cont(struct gsfile_symtable *, struct gspos, struct gsparsedfile_segment **, struct gsparsedline *);
static struct gsbc_code_item_type *gsbc_typecheck_api_expr(struct gspos, struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, gsinterned_string, gsinterned_string);

static
void
gstypes_type_check_code_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    static gsinterned_string gssymexpr, gssymforcecont, gssymstrictcont, gssymubcasecont, gssymimpprog;

    struct gsparsedline *pcode;
    struct gsparsedfile_segment *pseg;

    pseg = items[i].pseg;
    pcode = items[i].v;

    if (gssymceq(pcode->directive, gssymexpr, gssymcodedirective, ".expr")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_code_expr(symtable, &pseg, gsinput_next_line(&pseg, pcode));
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else if (gssymceq(pcode->directive, gssymforcecont, gssymcodedirective, ".forcecont")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_force_cont(symtable, &pseg, gsinput_next_line(&pseg, pcode));
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else if (gssymceq(pcode->directive, gssymstrictcont, gssymcodedirective, ".strictcont")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_strict_cont(symtable, &pseg, gsinput_next_line(&pseg, pcode));
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else if (gssymceq(pcode->directive, gssymubcasecont, gssymcodedirective, ".ubcasecont")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_ubcase_cont(symtable, pcode->pos, &pseg, gsinput_next_line(&pseg, pcode));
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else if (gssymceq(pcode->directive, gssymimpprog, gssymcodedirective, ".impprog")) {
        struct gsbc_code_item_type *type;

        if (pcode->numarguments < 1)
            gsfatal("%P: Not enough arguments to .impprog; missing primset", pcode->pos)
        ;
        if (pcode->numarguments < 2)
            gsfatal("%P: Not enough arguments to .impprog; missing API monad name", pcode->pos)
        ;
        type = gsbc_typecheck_api_expr(pcode->pos, symtable, &pseg, gsinput_next_line(&pseg, pcode), pcode->arguments[0], pcode->arguments[1]);
        gssymtable_set_code_type(symtable, pcode->label, type);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_type_check_code_item(%s)", pcode->pos, pcode->directive->name);
    }
}

static struct gstype *gsbc_typecheck_check_api_statement_type(struct gspos, struct gstype *, gsinterned_string, gsinterned_string, int *);

static struct gs_sys_global_block_suballoc_info gsbc_code_type_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Code item types",
    },
};

struct gsbc_typecheck_code_or_api_expr_closure {
    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];

    /* §section Type registers */
    struct gstype *tyregs[MAX_NUM_REGISTERS];
    struct gskind *tyregkinds[MAX_NUM_REGISTERS];

    /* §section Sub-Expressions */
    int ncodes;
    gsinterned_string coderegs[MAX_NUM_REGISTERS];
    struct gsbc_code_item_type *codetypes[MAX_NUM_REGISTERS];

    /* §section Coercion registers */
    struct gsbc_coercion_type *regcoerciontypes[MAX_NUM_REGISTERS];

    /* §section Data registers */
    struct gstype *regtypes[MAX_NUM_REGISTERS];

    /* §section Free type variables */
    int ntyfvs;
    struct gspos tyfvposs[MAX_NUM_REGISTERS];
    gsinterned_string tyfvnames[MAX_NUM_REGISTERS];
    struct gskind *tyfvkinds[MAX_NUM_REGISTERS];

    /* §section Free variables */
    int nfvs;
    int efv_bound[MAX_NUM_REGISTERS];
    gsinterned_string fvnames[MAX_NUM_REGISTERS];
    struct gspos fvposs[MAX_NUM_REGISTERS];
    struct gstype *fvtypes[MAX_NUM_REGISTERS];

    /* §section Stack for post-processing */
    int stacksize;
    struct gsparsedline *stack[MAX_NUM_REGISTERS];

    /* §section Type arguments */
    int ntyargs;
    gsinterned_string tyargnames[MAX_NUM_REGISTERS];
    struct gskind *tyargkinds[MAX_NUM_REGISTERS];

    /* §section Arguments */
    int nargs;
    struct gstype *argtypes[MAX_NUM_REGISTERS];
};

static void gsbc_setup_code_expr_closure(struct gsbc_typecheck_code_or_api_expr_closure *);

static int gsbc_typecheck_code_type_gvar_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_code_type_fv_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_code_type_arg_op(struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *);
static int gsbc_typecheck_code_type_let_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_subcode_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_coercion_gvar_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_data_gvar_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_data_fv_op(struct gsfile_symtable *, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_data_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl);
static int gsbc_typecheck_alloc_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *);
static int gsbc_typecheck_cont_push_op(struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *);
static struct gstype *gsbc_typecheck_expr_terminal_op(struct gsfile_symtable *, struct gsparsedline **, struct gsparsedfile_segment **, struct gsbc_typecheck_code_or_api_expr_closure *);

static int gsbc_typecheck_cont(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gstype **);
static int gsbc_typecheck_data_arg(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gstype **);
static int gsbc_typecheck_code_type_arg(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gstype **);

static struct gstype *gsbc_typecheck_compile_prim_type(struct gspos, struct gsfile_symtable *, char *);

static void gsbc_typecheck_free_type_variables(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gsbc_code_item_type *);
static void gsbc_typecheck_free_variables(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, struct gsbc_code_item_type *);

static struct gsbc_code_item_type *gsbc_typecheck_compile_code_item_type(int, struct gstype *, struct gstype *, struct gsbc_typecheck_code_or_api_expr_closure *);

static
struct gsbc_code_item_type *
gsbc_typecheck_code_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    struct gsbc_typecheck_code_or_api_expr_closure cl;

    struct gstype *calculated_type;

    gsbc_setup_code_expr_closure(&cl);

    /* §paragraph{Globals} */
    while (gsbc_typecheck_code_type_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_subcode_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_coercion_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Free Variables} */
    while (gsbc_typecheck_code_type_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Arguments} */
    while (
        gsbc_typecheck_code_type_arg_op(p, &cl)
        || gsbc_typecheck_code_type_let_op(p, &cl)
        || gsbc_typecheck_data_arg_op(p, &cl)
    )
        p = gsinput_next_line(ppseg, p)
    ;

    /* §paragraph{Lambda Body / Expression} */
    while (gsbc_typecheck_alloc_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_cont_push_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, &p, ppseg, &cl)))
        gsfatal(UNIMPL("%P: gsbc_typecheck_code_expr(%y)"), p->pos, p->directive)
    ;

    while (cl.stacksize) {
        p = cl.stack[--cl.stacksize];
        if (!(
            gsbc_typecheck_cont(&cl, p, &calculated_type)
            || gsbc_typecheck_data_arg(&cl, p, &calculated_type)
            || gsbc_typecheck_code_type_arg(&cl, p, &calculated_type)
        ))
            gsfatal(UNIMPL("%P: Unimplemented continuation, argument, or type argument op %y"), p->pos, p->directive)
        ;
    }

    return gsbc_typecheck_compile_code_item_type(gsbc_code_item_expr, 0, calculated_type, &cl);
}

static int gsbc_typecheck_cont_arg_op(struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *, struct gstype **);

static
struct gsbc_code_item_type *
gsbc_typecheck_force_cont(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    struct gsbc_typecheck_code_or_api_expr_closure cl;

    struct gstype *cont_arg_type;
    struct gstype *calculated_type;

    gsbc_setup_code_expr_closure(&cl);
    cont_arg_type = 0;

    /* §paragraph{Globals} */
    while (gsbc_typecheck_code_type_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_subcode_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_coercion_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Free Variables} */
    while (gsbc_typecheck_code_type_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Arguments} */
    if (gsbc_typecheck_cont_arg_op(p, &cl, &cont_arg_type)) {
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(cont_arg_type), gskind_unlifted_kind());
        p = gsinput_next_line(ppseg, p);
    } else {
        gsfatal("%P: No .karg in a .forcecont", p->pos);
    }

    /* §paragraph{Expression} */
    while (gsbc_typecheck_alloc_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_cont_push_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, &p, ppseg, &cl)))
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_force_cont(%y)", p->pos, p->directive)
    ;

    while (cl.stacksize)
        if (!gsbc_typecheck_cont(&cl, cl.stack[--cl.stacksize], &calculated_type))
            gsfatal(UNIMPL("%P: Un-implemented continuation op %y"), cl.stack[cl.stacksize]->pos, cl.stack[cl.stacksize]->directive)
    ;

    gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(calculated_type), gskind_lifted_kind());

    return gsbc_typecheck_compile_code_item_type(gsbc_code_item_force_cont, cont_arg_type, calculated_type, &cl);
}

static
struct gsbc_code_item_type *
gsbc_typecheck_strict_cont(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    struct gsbc_typecheck_code_or_api_expr_closure cl;

    struct gstype *cont_arg_type;
    struct gstype *calculated_type;

    gsbc_setup_code_expr_closure(&cl);
    cont_arg_type = 0;
    while (gsbc_typecheck_code_type_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_subcode_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_coercion_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    if (gsbc_typecheck_cont_arg_op(p, &cl, &cont_arg_type)) {
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(cont_arg_type), gskind_lifted_kind());
        p = gsinput_next_line(ppseg, p);
    } else {
        gsfatal("%P: No .karg in a .strictcont", p->pos);
    }
    while (gsbc_typecheck_alloc_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_cont_push_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, &p, ppseg, &cl)))
        gsfatal(UNIMPL("%P: gsbc_typecheck_force_cont(%y)"), p->pos, p->directive)
    ;

    while (cl.stacksize)
        if (!gsbc_typecheck_cont(&cl, cl.stack[--cl.stacksize], &calculated_type))
            gsfatal(UNIMPL("%P: Un-implemented continuation op %y"), cl.stack[cl.stacksize]->pos, cl.stack[cl.stacksize]->directive)
    ;

    gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(calculated_type), gskind_lifted_kind());

    return gsbc_typecheck_compile_code_item_type(gsbc_code_item_strict_cont, cont_arg_type, calculated_type, &cl);
}

static
int
gsbc_typecheck_cont_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gstype **pcont_arg_type)
{
    static gsinterned_string gssymkarg;

    int i;

    if (gssymceq(p->directive, gssymkarg, gssymcodeop, ".karg")) {
        int reg;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (*pcont_arg_type)
            gsfatal("%P: Duplicate .karg continuation argument", p->pos)
        ;
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        *pcont_arg_type = pcl->tyregs[reg];
        for (i = 1; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            *pcont_arg_type = gstype_apply(p->pos, *pcont_arg_type, pcl->tyregs[regarg]);
        }
        gstypes_kind_check_simple(p->pos, gstypes_calculate_kind(*pcont_arg_type));
        gsbc_typecheck_check_boxed(p->pos, *pcont_arg_type);
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = *pcont_arg_type;
        pcl->nregs++;
    } else {
        return 0;
    }

    return 1;
}

struct gsbc_typecheck_field_cont_closure {
    int nfields;
    struct gstype_field fields[MAX_NUM_REGISTERS];
};

static int gsbc_typecheck_field_cont_arg_op(struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *, struct gsbc_typecheck_field_cont_closure *);

static
struct gsbc_code_item_type *
gsbc_typecheck_ubcase_cont(struct gsfile_symtable *symtable, struct gspos case_pos, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    struct gsbc_typecheck_code_or_api_expr_closure cl;

    /* §paragraph{For constructors with boxed arguments} */
    struct gstype *cont_arg_type;
    /* §paragraph{For constructors with un-boxed product arguments} */
    struct gsbc_typecheck_field_cont_closure fcl;

    struct gstype *calculated_type;

    gsbc_setup_code_expr_closure(&cl);
    cont_arg_type = 0;
    fcl.nfields = 0;
    while (gsbc_typecheck_code_type_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_subcode_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_coercion_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    if (gsbc_typecheck_cont_arg_op(p, &cl, &cont_arg_type)) {
        p = gsinput_next_line(ppseg, p);
    } else {
        while (gsbc_typecheck_field_cont_arg_op(p, &cl, &fcl)) p = gsinput_next_line(ppseg, p);
    }
    while (gsbc_typecheck_alloc_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_cont_push_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, &p, ppseg, &cl)))
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_ubcase_cont(%y)", p->pos, p->directive)
    ;

    while (cl.stacksize)
        if (!gsbc_typecheck_cont(&cl, cl.stack[--cl.stacksize], &calculated_type))
            gsfatal(UNIMPL("%P: Un-implemented continuation op %y"), cl.stack[cl.stacksize]->pos, cl.stack[cl.stacksize]->directive)
    ;

    if (!cont_arg_type)
        cont_arg_type = gstypes_compile_ubproductv(case_pos, fcl.nfields, fcl.fields)
    ;

    return gsbc_typecheck_compile_code_item_type(gsbc_code_item_ubcase_cont, cont_arg_type, calculated_type, &cl);
}

static
void
gsbc_setup_code_expr_closure(struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    pcl->nregs = pcl->ntyfvs = pcl->nfvs = pcl->stacksize = pcl->ntyargs = pcl->nargs = pcl->ncodes = 0;
}

int
gsbc_typecheck_code_type_gvar_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymtygvar;

    if (gssymceq(p->directive, gssymtygvar, gssymcodeop, ".tygvar")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->tyregs[pcl->nregs] = gssymtable_get_type(symtable, p->label);
        if (!pcl->tyregs[pcl->nregs])
            gsfatal("Couldn't find type global %y", p->pos, p->label)
        ;
        pcl->tyregkinds[pcl->nregs] = gssymtable_get_type_expr_kind(symtable, p->label);
        if (!pcl->tyregkinds[pcl->nregs])
            gsfatal_unimpl(__FILE__, __LINE__, "%P: couldn't find kind of '%s'", p->pos, p->label->name)
        ;
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_code_type_fv_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymtyfv;

    if (gssymceq(p->directive, gssymtyfv, gssymcodeop, ".tyfv")) {
        struct gskind *fvkind;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        fvkind = gskind_compile(p->pos, p->arguments[0]);

        pcl->tyregkinds[pcl->nregs] = fvkind;
        pcl->tyregs[pcl->nregs] = gstypes_compile_type_var(p->pos, p->label, fvkind);
        pcl->nregs++;

        pcl->tyfvnames[pcl->ntyfvs] = p->label;
        pcl->tyfvkinds[pcl->ntyfvs] = fvkind;
        pcl->tyfvposs[pcl->ntyfvs] = p->pos;
        pcl->ntyfvs++;
    } else {
        return 0;
    }
    return 1;
}

static gsinterned_string gssymtyarg;

int
gsbc_typecheck_code_type_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    if (gssymceq(p->directive, gssymtyarg, gssymcodeop, ".tyarg")) {
        struct gskind *argkind;

        if (pcl->nregs >= MAX_NUM_REGISTERS) gsfatal("%P: Too many registers", p->pos);
        if (pcl->stacksize >= MAX_NUM_REGISTERS) gsfatal("%P: Internal type-checking stack overflow", p->pos);

        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        argkind = gskind_compile(p->pos, p->arguments[0]);

        pcl->tyregkinds[pcl->nregs] = argkind;
        pcl->tyregs[pcl->nregs] = gstypes_compile_type_var(p->pos, p->label, argkind);
        pcl->nregs++;

        pcl->stack[pcl->stacksize++] = p;
        pcl->tyargnames[pcl->ntyargs] = p->label;
        pcl->tyargkinds[pcl->ntyargs] = argkind;
        pcl->ntyargs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_code_type_arg(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gstype **pcalculated_type)
{
    if (p->directive == gssymtyarg) {
        if (!pcl->ntyargs--) gsfatal(UNIMPL("%P: Didn't add this type argument to the list of type arguments"), p->pos);

        *pcalculated_type = gstypes_compile_forall(p->pos, pcl->tyargnames[pcl->ntyargs], pcl->tyargkinds[pcl->ntyargs], *pcalculated_type);
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_code_type_let_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymtylet;

    int i;

    if (gssymceq(p->directive, gssymtylet, gssymcodeop, ".tylet")) {
        struct gstype *ty;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal(UNIMPL("%P: Register overflow"), p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "base");
        ty = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *arg;

            arg = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i])];
            ty = gstype_apply(p->pos, ty, arg);
        }
        if (ty->node == gstype_lambda) gsfatal("%P: Not enough arguments to %y", p->pos, p->arguments[0]);
        pcl->tyregs[pcl->nregs] = ty;
        pcl->tyregkinds[pcl->nregs] = gstypes_calculate_kind(ty);
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_subcode_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymopsubcode;

    if (gssymceq(p->directive, gssymopsubcode, gssymcodeop, ".subcode")) {
        if (pcl->ncodes >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many sub-expressions; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->coderegs[pcl->ncodes] = p->label;
        pcl->codetypes[pcl->ncodes] = gssymtable_get_code_type(symtable, p->label);
        if (!pcl->codetypes[pcl->ncodes])
            gsfatal("%P: Can't find type of sub-expression %y", p->pos, p->label)
        ;
        pcl->ncodes++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_coercion_gvar_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymcogvar;

    if (gssymceq(p->directive, gssymcogvar, gssymcodeop, ".cogvar")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->regcoerciontypes[pcl->nregs] = gssymtable_get_coercion_type(symtable, p->label);
        if (!pcl->regcoerciontypes[pcl->nregs])
            gsfatal("%P: Couldn't find type for coercion global %y", p->pos, p->label)
        ;
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_data_gvar_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymopgvar, gssymrune, gssymnatural;

    if (gssymceq(p->directive, gssymopgvar, gssymcodeop, ".gvar")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = gssymtable_get_data_type(symtable, p->label);
        if (!pcl->regtypes[pcl->nregs])
            gsfatal("%P: Couldn't find type for global %y", p->pos, p->label)
        ;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymrune, gssymcodeop, ".rune")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = gstypes_compile_prim(p->pos, gsprim_type_defined, "rune.prim", "rune", gskind_unlifted_kind());
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymnatural, gssymcodeop, ".natural")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = gstypes_compile_prim(p->pos, gsprim_type_defined, "natural.prim", "natural", gskind_unlifted_kind());
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_data_fv_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymopfv, gssymopefv;

    int i;

    if (
        gssymceq(p->directive, gssymopfv, gssymcodeop, ".fv")
        || gssymceq(p->directive, gssymopefv, gssymcodeop, ".efv")
    ) {
        int reg;
        struct gstype *fvtype;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "type");
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        fvtype = pcl->tyregs[reg];
        if (!fvtype)
            gsfatal("%P: Register %y is not a type", p->pos, p->arguments[0])
        ;
        for (i = 1; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            fvtype = gstype_apply(p->pos, fvtype, pcl->tyregs[regarg]);
        }
        if (p->directive == gssymopefv)
            gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(fvtype), gskind_unlifted_kind())
        ;
        pcl->regtypes[pcl->nregs] = fvtype;
        pcl->nregs++;

        pcl->efv_bound[pcl->nfvs] = p->directive == gssymopefv;
        pcl->fvtypes[pcl->nfvs] = fvtype;
        pcl->fvposs[pcl->nfvs] = p->pos;
        pcl->fvnames[pcl->nfvs] = p->label;
        pcl->nfvs++;
    } else {
        return 0;
    }
    return 1;
}

static gsinterned_string gssymarg, gssymlarg;

int
gsbc_typecheck_data_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    int i;

    if (
        gssymceq(p->directive, gssymarg, gssymcodeop, ".arg")
        || gssymceq(p->directive, gssymlarg, gssymcodeop, ".larg")
    ) {
        int reg;
        struct gstype *argtype;

        if (pcl->nregs >= MAX_NUM_REGISTERS) gsfatal("%P: Too many registers", p->pos);
        if (pcl->stacksize >= MAX_NUM_REGISTERS) gsfatal("%P: Internal type-checking stack overflow", p->pos);
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "var");
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        argtype = pcl->tyregs[reg];
        if (!argtype)
            gsfatal("%P: Register %y is not a type", p->pos, p->arguments[0])\
        ;
        for (i = 1; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            argtype = gstype_apply(p->pos, argtype, pcl->tyregs[regarg]);
        }
        gstypes_kind_check_simple(p->pos, gstypes_calculate_kind(argtype));
        gsbc_typecheck_check_boxed(p->pos, argtype);
        pcl->regtypes[pcl->nregs] = argtype;
        pcl->nregs++;

        pcl->argtypes[pcl->nargs] = argtype;
        pcl->stack[pcl->stacksize++] = p;
        pcl->nargs++;
    } else {
        return 0;
    }
    return 1;
}

static void gsbc_typecheck_validate_prim_type(struct gspos, gsinterned_string, struct gstype *);

static void gsbc_typecheck_check_num_constrs(struct gspos, long, long);
static void gsbc_typecheck_check_constr(struct gspos, struct gspos, int, gsinterned_string, gsinterned_string, gsinterned_string, gsinterned_string);

static struct gstype *gsbc_typecheck_case(struct gspos, struct gsfile_symtable *, struct gsparsedline **, struct gsparsedfile_segment **, struct gsbc_typecheck_code_or_api_expr_closure *, struct gstype *constr_arg_type);
static struct gstype *gsbc_typecheck_default(struct gspos, struct gsfile_symtable *, struct gsparsedline **, struct gsparsedfile_segment **, struct gsbc_typecheck_code_or_api_expr_closure *);

static
struct gstype *
gsbc_typecheck_expr_terminal_op(struct gsfile_symtable *symtable, struct gsparsedline **pp, struct gsparsedfile_segment **ppseg, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymundef, gssymyield, gssymenter, gssymubprim, gssymlprim, gssymanalyze, gssymdanalyze, gssymcase, gssymdefault;

    int i;

    struct gstype *calculated_type;

    calculated_type = 0;
    if (gssymceq((*pp)->directive, gssymundef, gssymcodeop, ".undef")) {
        int reg;
        struct gskind *kind;

        gsargcheck(*pp, 0, "type");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        calculated_type = pcl->tyregs[reg];
        kind = pcl->tyregkinds[reg];
        for (i = 1; i < (*pp)->numarguments; i++) {
            int regarg;
            struct gstype *argtype;
            struct gskind *argkind;

            regarg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            argtype = pcl->tyregs[regarg];
            argkind = pcl->tyregkinds[regarg];

            if (kind->node != gskind_exponential)
                gsfatal("%P: Too many arguments to %y", (*pp)->pos, (*pp)->arguments[0])
            ;

            gstypes_kind_check_fail((*pp)->pos, argkind, kind->args[1]);

            calculated_type = gstype_apply((*pp)->pos, calculated_type, argtype);
            kind = kind->args[0];
        }
        gstypes_kind_check_fail((*pp)->pos, kind, gskind_lifted_kind());
    } else if (gssymceq((*pp)->directive, gssymyield, gssymcodeop, ".yield")) {
        int reg;
        struct gskind *kind;

        gsargcheck(*pp, 0, "var");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        calculated_type = pcl->regtypes[reg];
        for (i = 1; i < (*pp)->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            if (!pcl->tyregs[regarg])
                gsfatal("%P: %s doesn't seem to be a type register", (*pp)->pos, (*pp)->arguments[i]->name)
            ;
            calculated_type = gstype_instantiate((*pp)->pos, calculated_type, pcl->tyregs[regarg]);
        }

        kind = gstypes_calculate_kind(calculated_type);
        gstypes_kind_check_fail((*pp)->pos, kind, gskind_unlifted_kind());
    } else if (gssymceq((*pp)->directive, gssymenter, gssymcodeop, ".enter")) {
        int reg;
        struct gskind *kind;

        gsargcheck(*pp, 0, "var");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        calculated_type = pcl->regtypes[reg];
        for (i = 1; i < (*pp)->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            if (!pcl->tyregs[regarg])
                gsfatal("%P: %y doesn't seem to be a type register", (*pp)->pos, (*pp)->arguments[i])
            ;
            calculated_type = gstype_instantiate((*pp)->pos, calculated_type, pcl->tyregs[regarg]);
        }

        kind = gstypes_calculate_kind(calculated_type);
        gstypes_kind_check_fail((*pp)->pos, kind, gskind_lifted_kind());
    } else if (gssymceq((*pp)->directive, gssymubprim, gssymcodeop, ".ubprim")) {
        struct gsregistered_primset *prims;
        int tyreg;
        int first_arg_pos;

        gsargcheck(*pp, 2, "type");
        tyreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[2]);
        calculated_type = pcl->tyregs[tyreg];
        gsbc_typecheck_validate_prim_type((*pp)->pos, (*pp)->arguments[0], calculated_type);

        gsargcheck(*pp, 0, "primset");
        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            struct gsregistered_prim *prim;
            struct gstype *expected_type;

            if (!(prim = gsprims_lookup_prim(prims, (*pp)->arguments[1]->name)))
                gsfatal("%P: Primitive set %s has no prim %y", (*pp)->pos, prims->name, (*pp)->arguments[1])
            ;

            if (prim->group != gsprim_operation_unboxed)
                gsfatal("%P: Primitive %s in primset %s is not an unboxed primitive", (*pp)->pos, prim->name, prims->name)
            ;

            expected_type = gsbc_typecheck_compile_prim_type((*pp)->pos, symtable, prim->type);
            gstypes_type_check_type_fail((*pp)->pos, calculated_type, expected_type);
        }

        for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++) {
            int tyargreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            calculated_type = gstype_instantiate((*pp)->pos, calculated_type, pcl->tyregs[tyargreg]);
        }
        if (i < (*pp)->numarguments) i++;
        first_arg_pos = i;
        for (; i < (*pp)->numarguments; i++) {
            int argreg;
            struct gstype *argtype;

            argreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            argtype = pcl->regtypes[argreg];

            if (calculated_type->node == gstype_fun) {
                struct gstype_fun *fun;

                fun = (struct gstype_fun *)calculated_type;
                gstypes_type_check_type_fail((*pp)->pos, argtype, fun->tyarg);
                calculated_type = fun->tyres;
            } else {
                gsfatal("%P: Too many arguments to %y (max %d; got %d)", (*pp)->pos, (*pp)->arguments[2], i - first_arg_pos, (*pp)->numarguments - first_arg_pos);
            }
        }
        if (calculated_type->node != gstype_ubsum)
            gsfatal("%P: Result of .ubprim is not an unboxed sum type", (*pp)->pos)
        ;
    } else if (gssymceq((*pp)->directive, gssymlprim, gssymcodeop, ".lprim")) {
        struct gsregistered_primset *prims;
        int tyreg;
        int first_arg_pos;

        gsargcheck(*pp, 2, "type");
        tyreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[2]);
        calculated_type = pcl->tyregs[tyreg];
        gsbc_typecheck_validate_prim_type((*pp)->pos, (*pp)->arguments[0], calculated_type);

        gsargcheck(*pp, 0, "primset");
        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            struct gsregistered_prim *prim;
            struct gstype *expected_type;

            if (!(prim = gsprims_lookup_prim(prims, (*pp)->arguments[1]->name)))
                gsfatal("%P: Primitive set %s has no prim %y", (*pp)->pos, prims->name, (*pp)->arguments[1])
            ;

            if (prim->group != gsprim_operation_lifted)
                gsfatal("%P: Primitive %s in primset %s is not a lifted primitive", (*pp)->pos, prim->name, prims->name)
            ;

            expected_type = gsbc_typecheck_compile_prim_type((*pp)->pos, symtable, prim->type);
            gstypes_type_check_type_fail((*pp)->pos, calculated_type, expected_type);
        }

        for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++) {
            int tyargreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            calculated_type = gstype_instantiate((*pp)->pos, calculated_type, pcl->tyregs[tyargreg]);
        }
        if (i < (*pp)->numarguments) i++;
        first_arg_pos = i;
        for (; i < (*pp)->numarguments; i++) {
            int argreg;
            struct gstype *argtype;

            argreg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
            argtype = pcl->regtypes[argreg];

            if (calculated_type->node == gstype_fun) {
                struct gstype_fun *fun;

                fun = (struct gstype_fun *)calculated_type;
                gstypes_type_check_type_fail((*pp)->pos, argtype, fun->tyarg);
                calculated_type = fun->tyres;
            } else {
                gsfatal("%P: Too many arguments to %y (max %d; got %d)", (*pp)->pos, (*pp)->arguments[2], i - first_arg_pos, (*pp)->numarguments - first_arg_pos);
            }
        }
        gstypes_kind_check_fail((*pp)->pos, gstypes_calculate_kind(calculated_type), gskind_lifted_kind());
        gsbc_typecheck_check_boxed((*pp)->pos, calculated_type);
    } else if (gssymceq((*pp)->directive, gssymanalyze, gssymcodeop, ".analyze")) {
        int reg;
        struct gstype_sum *sum;
        struct gsparsedline *panalyze;
        int constr;

        gsargcheck(*pp, 0, "scrutinee");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        if (pcl->regtypes[reg]->node != gstype_sum)
            gsfatal("%P: Scrutinee to .analyze does not have a simple, known sum type", (*pp)->pos)
        ;
        sum = (struct gstype_sum *)pcl->regtypes[reg];
        gsbc_typecheck_check_num_constrs((*pp)->pos, (*pp)->numarguments - 1, sum->numconstrs);
        panalyze = *pp;
        for (constr = 0; constr < sum->numconstrs; constr++) {
            struct gstype *casetype;
            struct gspos case_pos;
            int nregs;

            *pp = gsinput_next_line(ppseg, *pp);
            case_pos = (*pp)->pos;
            if (!gssymceq((*pp)->directive, gssymcase, gssymcodeop, ".case"))
                gsfatal("%P: Expected .case %y next", (*pp)->pos, panalyze->arguments[1 + constr])
            ;

            gsargcheck(*pp, 0, "Constructor");
            gsbc_typecheck_check_constr(panalyze->pos, (*pp)->pos, constr, constr > 0 ? sum->constrs[constr - 1].name : 0, (*pp)->arguments[0], panalyze->arguments[1 + constr], sum->constrs[constr].name);

            *pp = gsinput_next_line(ppseg, *pp);
            nregs = pcl->nregs;
            casetype = gsbc_typecheck_case(case_pos, symtable, pp, ppseg, pcl, sum->constrs[constr].argtype);
            pcl->nregs = nregs;

            if (calculated_type)
                gstypes_type_check_type_fail(panalyze->pos, casetype, calculated_type)
            ; else
                calculated_type = casetype
            ;
        }
    } else if (gssymceq((*pp)->directive, gssymdanalyze, gssymcodeop, ".danalyze")) {
        int reg;
        struct gstype_sum *sum;
        struct gsparsedline *panalyze;
        struct gspos default_pos;
        int prevconstr, casenum;
        int nregs;

        gsargcheck(*pp, 0, "scrutinee");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        if (pcl->regtypes[reg]->node != gstype_sum)
            gsfatal(UNIMPL("%P: Scrutinee to .danalyze does not have a simple, known sum type"), (*pp)->pos)
        ;
        sum = (struct gstype_sum *)pcl->regtypes[reg];
        panalyze = *pp;

        *pp = gsinput_next_line(ppseg, *pp);
        default_pos = (*pp)->pos;
        if (!gssymceq((*pp)->directive, gssymdefault, gssymcodeop, ".default"))
            gsfatal("%P: Expected .default next", (*pp)->pos)
        ;

        *pp = gsinput_next_line(ppseg, *pp);
        nregs = pcl->nregs;
        calculated_type = gsbc_typecheck_default(default_pos, symtable, pp, ppseg, pcl);
        pcl->nregs = nregs;

        prevconstr = -1;
        for (casenum = 0; casenum < panalyze->numarguments - 1; casenum++) {
            struct gstype *casetype;
            struct gspos case_pos;
            int constr;

            *pp = gsinput_next_line(ppseg, *pp);
            case_pos = (*pp)->pos;
            if (!gssymceq((*pp)->directive, gssymcase, gssymcodeop, ".case"))
                gsfatal("%P: Expected .case %y next", (*pp)->pos, panalyze->arguments[1 + casenum])
            ;

            gsargcheck(*pp, 0, "Constructor");
            if (casenum > 0)
                gsfatal(UNIMPL("%P: Check order of cases in a .danalyze"), case_pos)
            ;
            constr = gsbc_typecheck_find_constr(case_pos, prevconstr, sum, (*pp)->arguments[0]);

            *pp = gsinput_next_line(ppseg, *pp);
            nregs = pcl->nregs;
            casetype = gsbc_typecheck_case(case_pos, symtable, pp, ppseg, pcl, sum->constrs[constr].argtype);
            pcl->nregs = nregs;

            gstypes_type_check_type_fail(panalyze->pos, casetype, calculated_type);
            prevconstr = constr;
        }
    } else {
        return 0;
    }
    return calculated_type;
}

static
void
gsbc_typecheck_check_num_constrs(struct gspos pos, long nactual, long nexpected)
{
    if (nactual < nexpected)
        gsfatal("%P: Too few cases for .analyze; got %d but sum type has %d", pos, nactual, nexpected)
    ;
    if (nactual > nexpected)
        gsfatal("%P: Too many cases for .analyze; got %d but sum type has %d", pos, nactual, nexpected)
    ;
}

static
void
gsbc_typecheck_check_constr(struct gspos analyzepos, struct gspos casepos, int constr, gsinterned_string prevconstr, gsinterned_string analyzeconstr, gsinterned_string caseconstr, gsinterned_string sumconstr)
{
    if (analyzeconstr != sumconstr)
        gsfatal("%P: Wrong constructor #%d: got %y; sum type has %y", analyzepos, constr, analyzeconstr, sumconstr)
    ;
    if (analyzeconstr != caseconstr)
        gsfatal("%P: Wrong constructor: got %y; .analyze has %y", casepos, caseconstr, analyzeconstr)
    ;
    if (prevconstr) {
        if (prevconstr == analyzeconstr)
            gsfatal("%P: Duplicate constructor %y", analyzepos)
        ;
        if (strcmp(prevconstr->name, analyzeconstr->name) > 0)
            gsfatal("%P: Constructors out of order; %y should follow %y", analyzepos, prevconstr, analyzeconstr)
        ;
    }
}

int
gsbc_typecheck_find_constr(struct gspos case_pos, int prevconstr, struct gstype_sum *sum, gsinterned_string constrname)
{
    int constrnum;

    for (constrnum = prevconstr + 1; constrnum < sum->numconstrs; constrnum++) {
        if (constrnum > 0) {
            if (sum->constrs[constrnum - 1].name == sum->constrs[constrnum].name)
                gsfatal("%P: Duplicate constructor %y", sum->constrs[constrnum].name)
            ;
            if (strcmp(sum->constrs[constrnum - 1].name->name, sum->constrs[constrnum].name->name) > 0)
                gsfatal("%P: Constructor %y should follow %y", sum->constrs[constrnum - 1].name, sum->constrs[constrnum].name)
            ;
        }
        if (sum->constrs[constrnum].name == constrname)
            return constrnum
        ;
    }
    gsfatal("%P: Sum type has no constructor %y", case_pos, constrname);
    return -1;
}

struct gsbc_typecheck_existential_cont_closure {
    int nargs;
    struct gspos argpos[MAX_NUM_REGISTERS];
    gsinterned_string argnames[MAX_NUM_REGISTERS];
    struct gskind *argkinds[MAX_NUM_REGISTERS];
};

static void gsbc_typecheck_init_existential_cont_closure(struct gsbc_typecheck_existential_cont_closure *);
static int gsbc_typecheck_cont_type_arg_op(struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *, struct gsbc_typecheck_existential_cont_closure *);
static struct gstype *gsbc_typecheck_exists_args(struct gsbc_typecheck_existential_cont_closure *, struct gstype *, struct gstype *);

static
struct gstype *
gsbc_typecheck_case(struct gspos case_pos, struct gsfile_symtable *symtable, struct gsparsedline **pp, struct gsparsedfile_segment **ppseg, struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gstype *constr_arg_type)
{
    /* §paragraph{For constructors with existential type arguments} */
    struct gsbc_typecheck_existential_cont_closure excl;
    /* §paragraph{For constructors with boxed arguments} */
    struct gstype *cont_arg_type;
    /* §paragraph{For constructors with un-boxed product arguments} */
    struct gsbc_typecheck_field_cont_closure fcl;

    int saved_stacksize;

    struct gstype *calculated_type;

    struct gspos karg_pos;

    cont_arg_type = 0;
    fcl.nfields = 0;
    saved_stacksize = pcl->stacksize;
    calculated_type = 0;
    gsbc_typecheck_init_existential_cont_closure(&excl);
    while (gsbc_typecheck_cont_type_arg_op(*pp, pcl, &excl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_typecheck_code_type_let_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    if (gsbc_typecheck_cont_arg_op(*pp, pcl, &cont_arg_type)) {
        karg_pos = (*pp)->pos;
        *pp = gsinput_next_line(ppseg, *pp);
    } else {
        while (gsbc_typecheck_field_cont_arg_op(*pp, pcl, &fcl)) *pp = gsinput_next_line(ppseg, *pp);
    }
    while (gsbc_typecheck_alloc_op(symtable, *pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_typecheck_cont_push_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, pp, ppseg, pcl)))
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_case(%y)", (*pp)->pos, (*pp)->directive)
    ;

    while (pcl->stacksize > saved_stacksize)
        if (!gsbc_typecheck_cont(pcl, pcl->stack[--pcl->stacksize], &calculated_type))
            gsfatal(UNIMPL("%P: Un-implemented continuation op %y"), pcl->stack[pcl->stacksize]->pos, pcl->stack[pcl->stacksize]->directive)
    ;

    if (!cont_arg_type)
        cont_arg_type = gstypes_compile_ubproductv(case_pos, fcl.nfields, fcl.fields)
    ;

    cont_arg_type = gsbc_typecheck_exists_args(&excl, cont_arg_type, calculated_type);

    gstypes_type_check_type_fail(case_pos, cont_arg_type, constr_arg_type);

    return calculated_type;
}

static
void
gsbc_typecheck_init_existential_cont_closure(struct gsbc_typecheck_existential_cont_closure *pexcl)
{
    pexcl->nargs = 0;
}

static
int
gsbc_typecheck_cont_type_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsbc_typecheck_existential_cont_closure *pexcl)
{
    static gsinterned_string gssymexkarg;

    if (gssymceq(p->directive, gssymexkarg, gssymcodeop, ".exkarg")) {
        struct gskind *argkind;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;
        pcl->regs[pcl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        argkind = gskind_compile(p->pos, p->arguments[0]);

        pcl->tyregkinds[pcl->nregs] = argkind;
        pcl->tyregs[pcl->nregs] = gstypes_compile_type_var(p->pos, p->label, argkind);
        pcl->nregs++;

        pexcl->argnames[pexcl->nargs] = p->label;
        pexcl->argkinds[pexcl->nargs] = argkind;
        pexcl->argpos[pexcl->nargs] = p->pos;
        pexcl->nargs++;
    } else {
        return 0;
    }

    return 1;
}

static
struct gstype *
gsbc_typecheck_exists_args(struct gsbc_typecheck_existential_cont_closure *pexcl, struct gstype *cont_arg_type, struct gstype *calculated_type)
{
    while (pexcl->nargs--) {
        struct gspos pos = pexcl->argpos[pexcl->nargs];
        gsinterned_string var = pexcl->argnames[pexcl->nargs];

        if (gstypes_is_ftyvar(var, calculated_type))
            gsfatal("%P: .exkarg-bound type variable %y free in type of .case body", pos, var)
        ;
        cont_arg_type = gstypes_compile_exists(pos, var, pexcl->argkinds[pexcl->nargs], cont_arg_type);
    }

    return cont_arg_type;
}

static
struct gstype *
gsbc_typecheck_default(struct gspos default_pos, struct gsfile_symtable *symtable, struct gsparsedline **pp, struct gsparsedfile_segment **ppseg, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    int saved_stacksize;

    struct gstype *calculated_type;

    saved_stacksize = pcl->stacksize;
    calculated_type = 0;
    while (gsbc_typecheck_alloc_op(symtable, *pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_typecheck_cont_push_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    if (!(calculated_type = gsbc_typecheck_expr_terminal_op(symtable, pp, ppseg, pcl)))
        gsfatal(UNIMPL("%P: gsbc_typecheck_default(%y)"), (*pp)->pos, (*pp)->directive)
    ;

    while (pcl->stacksize > saved_stacksize)
        if (!gsbc_typecheck_cont(pcl, pcl->stack[--pcl->stacksize], &calculated_type))
            gsfatal(UNIMPL("%P: Un-implemented continuation op %y"), pcl->stack[pcl->stacksize]->pos, pcl->stack[pcl->stacksize]->directive)
    ;

    return calculated_type;

}

static
int
gsbc_typecheck_field_cont_arg_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsbc_typecheck_field_cont_closure *pfcl)
{
    static gsinterned_string gssymfkarg;

    int i;

    if (gssymceq(p->directive, gssymfkarg, gssymcodeop, ".fkarg")) {
        int reg;
        struct gstype *type;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (pfcl->nfields >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many .fkargs; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->regs[pcl->nregs] = p->label;
        pfcl->fields[pfcl->nfields].name = p->arguments[0];
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1]);
        type = pcl->tyregs[reg];
        for (i = 2; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            type = gstype_apply(p->pos, type, pcl->tyregs[regarg]);
        }
        gsbc_typecheck_check_boxed(p->pos, type);
        pfcl->fields[pfcl->nfields].type = type;
        pcl->regtypes[pcl->nregs] = type;

        pcl->nregs++;
        pfcl->nfields++;
    } else {
        return 0;
    }

    return 1;
}

static struct gstype *gsbc_find_field_in_product(struct gspos, struct gstype_product *, gsinterned_string);

#define CHECK_PHASE(ph, nm) \
    do { \
        if (pcl->regtype > ph) \
            gsfatal("%P: Too late to add %s", p->pos, nm) \
        ; \
        pcl->regtype = ph; \
    } while (0)

#define CHECK_NUM_REGS(nregs) \
    do { \
        if (nregs >= MAX_NUM_REGISTERS) \
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS) \
        ; \
    } while (0)

static
int
gsbc_typecheck_alloc_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    static gsinterned_string gssymalloc, gssymprim, gssymimpprim, gssymconstr, gssymexconstr, gssymrecord, gssymlrecord, gssymfield, gssymlfield, gssymundefined, gssymcast, gssymapply;

    int i;

    if (gssymceq(p->directive, gssymalloc, gssymcodeop, ".alloc")) {
        int creg = 0;
        struct gsbc_code_item_type *cty;
        struct gstype *alloc_type;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 0, "code");
        creg = gsbc_find_register(p, pcl->coderegs, pcl->ncodes, p->arguments[0]);
        cty = gsbc_typecheck_copy_code_item_type(pcl->codetypes[creg]);

        gsbc_typecheck_free_type_variables(pcl, p, cty);
        gsbc_typecheck_free_variables(pcl, p, cty);
        alloc_type = cty->result_type;

        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(alloc_type), gskind_lifted_kind());
        gsbc_typecheck_check_boxed(p->pos, alloc_type);

        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = alloc_type;

        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymprim, gssymcodeop, ".prim")) {
        struct gsregistered_primset *prims;
        struct gstype *type;
        int first_arg_pos;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 2, "type");
        type = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[2])];
        gsbc_typecheck_validate_prim_type(p->pos, p->arguments[0], type);

        gsargcheck(p, 0, "primset");
        if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
            struct gsregistered_prim *prim;
            struct gstype *expected_type;

            if (!(prim = gsprims_lookup_prim(prims, p->arguments[1]->name)))
                gsfatal("%P: Primitive set %s has no prim %y", p->pos, prims->name, p->arguments[1])
            ;

            if (prim->group != gsprim_operation)
                gsfatal("%P: Primitive %s in primset %s is not an API primitive", p->pos, prim->name, prims->name)
            ;

            expected_type = gsbc_typecheck_compile_prim_type(p->pos, symtable, prim->type);
            gstypes_type_check_type_fail(p->pos, type, expected_type);
        }

        for (i = 3; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
            int tyargreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            type = gstype_instantiate(p->pos, type, pcl->tyregs[tyargreg]);
        }
        if (i < p->numarguments) i++;
        first_arg_pos = i;
        for (; i < p->numarguments; i++) {
            int argreg;
            struct gstype *argtype;

            argreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            argtype = pcl->regtypes[argreg];

            if (type->node == gstype_fun) {
                struct gstype_fun *fun;

                fun = (struct gstype_fun *)type;
                gstypes_type_check_type_fail(p->pos, argtype, fun->tyarg);
                type = fun->tyres;
            } else {
                gsfatal("%P: Too many arguments to %y (max %d; got %d)", p->pos, p->arguments[1], i - first_arg_pos, p->numarguments - first_arg_pos);
            }
        }

        pcl->regs[pcl->nregs] = p->label;
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(type), gskind_unlifted_kind());
        gsbc_typecheck_check_boxed(p->pos, type);
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymimpprim, gssymcodeop, ".impprim")) {
        struct gsregistered_primset *prims;
        struct gstype *type;
        int tyreg;
        int first_arg_pos;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;

        gsargcheck(p, 3, "type");
        tyreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[3]);
        type = pcl->tyregs[tyreg];

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
            int tyargreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            type = gstype_instantiate(p->pos, type, pcl->tyregs[tyargreg]);
        }
        if (i < p->numarguments) i++;
        first_arg_pos = i;
        for (; i < p->numarguments; i++) {
            int argreg;
            struct gstype *argtype;

            argreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            argtype = pcl->regtypes[argreg];

            if (type->node == gstype_fun) {
                struct gstype_fun *fun;

                fun = (struct gstype_fun *)type;
                gstypes_type_check_type_fail(p->pos, argtype, fun->tyarg);
                type = fun->tyres;
            } else {
                gsfatal("%P: Too many arguments to %s (max %d; got %d)", p->pos, p->arguments[2]->name, i - first_arg_pos, p->numarguments - first_arg_pos);
            }
        }

        pcl->regs[pcl->nregs] = p->label;
        gsbc_typecheck_check_api_statement_type(p->pos, type, p->arguments[0], p->arguments[1], 0);
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else if (
        gssymceq(p->directive, gssymconstr, gssymcodeop, ".constr")
        || gssymceq(p->directive, gssymexconstr, gssymcodeop, ".exconstr")
    ) {
        int tyreg;
        int constrnum;
        int first_val_arg;
        struct gstype *type, *argtype;
        struct gstype_sum *sum;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        gsargcheck(p, 0, "Type");
        tyreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        type = pcl->tyregs[tyreg];
        if (type->node != gstype_sum)
            gsfatal("%P: Type %y is not a sum type (maybe it's lifted, or abstract?)", p->pos, p->arguments[0])
        ;
        sum = (struct gstype_sum *)type;

        gsargcheck(p, 1, "constructor");
        constrnum = gstypes_find_constr_in_sum(p->pos, p->arguments[0], sum, p->arguments[1]);
        argtype = sum->constrs[constrnum].argtype;

        first_val_arg = 2;
        if (p->directive == gssymexconstr) {
            for (i = first_val_arg; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
                struct gstype_exists *exists;
                struct gstype *tyarg;

                if (argtype->node != gstype_exists)
                    gsfatal("%P: Too many existential type arguments to %y (max %d)", p->pos, p->arguments[1], i - first_val_arg)
                ;
                exists = (struct gstype_exists *)argtype;

                tyarg = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i])];
                gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(tyarg), exists->kind);
                argtype = gstypes_subst(p->pos, exists->body, exists->var, tyarg);
            }
            if (i < p->numarguments) i++;
            first_val_arg = i;
        }
        if (p->numarguments == first_val_arg + 1)
            gsfatal(UNIMPL("%P: gsbc_typecheck_alloc_op: %y with boxed argument"), p->pos, p->directive)
        ; else {
            int nfields;
            struct gstype_field fields[MAX_NUM_REGISTERS];

            if ((p->numarguments - first_val_arg) % 2)
                gsfatal("%P: Odd number of arguments to .constr when expecting field/constructor pairs", p->pos)
            ;
            nfields = 0;
            for (i = 0; first_val_arg + i * 2 < p->numarguments; i++) {
                int argreg;

                nfields++;
                fields[i].name = p->arguments[first_val_arg + i * 2];
                argreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[first_val_arg + i * 2 + 1]);
                fields[i].type = pcl->regtypes[argreg];
            }
            gsbc_check_field_order(p->pos, nfields, fields);
            gstypes_type_check_type_fail(p->pos, gstypes_compile_ubproductv(p->pos, nfields, fields), argtype);
        }

        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else if (
        gssymceq(p->directive, gssymrecord, gssymcodeop, ".record")
        || gssymceq(p->directive, gssymlrecord, gssymcodeop, ".lrecord")
    ) {
        struct gstype *type;
        struct gstype_field fields[MAX_NUM_REGISTERS];
        int nfields;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers", p->pos)
        ;

        nfields = p->numarguments / 2;
        for (i = 0; i < p->numarguments; i += 2) {
            int reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i + 1]);
            fields[i / 2].name = p->arguments[i];
            fields[i / 2].type = pcl->regtypes[reg];
        }

        type = gstypes_compile_productv(p->pos, nfields, fields);
        if (p->directive == gssymlrecord)
            type = gstypes_compile_lift(p->pos, type)
        ;

        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else if (
        gssymceq(p->directive, gssymfield, gssymcodeop, ".field")
        || gssymceq(p->directive, gssymlfield, gssymcodeop, ".lfield")
    ) {
        int reg;
        struct gstype *type, *fieldtype;
        struct gstype_product *product;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 0, "Field");
        gsargcheck(p, 1, "Record");
        reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1]);
        type = pcl->regtypes[reg];
        if (!type) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1]);

        if (p->directive == gssymlfield) {
            if (type->node != gstype_lift)
                gsfatal("%P: %y does not have a lifted type (maybe it's an un-lifted product and you meant .field?)", p->pos, p->arguments[1])
            ;
            type = ((struct gstype_lift *)type)->arg;
        }
        if (type->node != gstype_product)
            gsfatal("%P: %y does not have a product type (maybe it's lifted and you meant .lfield?)", p->pos, p->arguments[1])
        ;
        product = (struct gstype_product *)type;
        fieldtype = gsbc_find_field_in_product(p->pos, product, p->arguments[0]);
        if (!fieldtype) gsfatal("%P: Type of %y has no field %y", p->pos, p->arguments[1], p->arguments[0]);

        if (p->directive == gssymlfield)
            gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(fieldtype), gskind_lifted_kind())
        ;

        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = fieldtype;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymundefined, gssymcodeop, ".undefined")) {
        struct gstype *type, *tyarg;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 0, "Type");
        type = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            tyarg = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i])];
            type = gstype_apply(p->pos, type, tyarg);
        }
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(type), gskind_lifted_kind());
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymcast, gssymcodeop, ".cast")) {
        struct gstype *target_type;
        struct gsbc_coercion_type *coercion_type;
        struct gstype *src_type, *dest_type;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 0, "Target");
        target_type = pcl->regtypes[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0])];

        gsargcheck(p, 1, "Coercion");
        coercion_type = pcl->regcoerciontypes[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1])];
        
        src_type = coercion_type->source;
        dest_type = coercion_type->dest;
        for (i = 2; i < p->numarguments; i++) {
            gsfatal(UNIMPL("%P: Handle type arguments to coercion next"), p->pos);
        }
        gstypes_type_check_type_fail(p->pos, target_type, src_type);

        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = dest_type;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymapply, gssymcodeop, ".apply")) {
        int first_arg;
        struct gstype *type;

        CHECK_NUM_REGS(pcl->nregs);

        gsargcheck(p, 0, "Function");
        type = pcl->regtypes[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
            struct gstype *tyarg;

            tyarg = pcl->tyregs[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i])];
            type = gstype_instantiate(p->pos, type, tyarg);
        }
        if (i < p->numarguments) i++;
        first_arg = i;
        for (; i < p->numarguments; i++) {
            struct gstype *tyarg;
            struct gstype_fun *fun;
            int is_lifted;

            tyarg = pcl->regtypes[gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i])];
            is_lifted = 0;
            if (type->node == gstype_lift) {
                struct gstype_lift *lift = (struct gstype_lift *)type;

                is_lifted = 1;
                type = lift->arg;
            }
            if (type->node != gstype_fun)
                gsfatal("%P: Too many arguments to %y%s (max %d)", p->pos, p->arguments[0], type->node == gstype_forall ? " (type is polymorphic)" : "", i - first_arg)
            ;
            fun = (struct gstype_fun *)type;
            gstypes_type_check_type_fail(p->pos, tyarg, fun->tyarg);
            type = fun->tyres;
            if (is_lifted)
                gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(type), gskind_lifted_kind())
            ;
        }
        gsbc_typecheck_check_boxed(p->pos, type);
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(type), gskind_lifted_kind());
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = type;
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gstypes_find_constr_in_sum(struct gspos pos, gsinterned_string tyname, struct gstype_sum *sum, gsinterned_string c)
{
    int i;

    for (i = 1; i < sum->numconstrs; i++) {
        struct gstype_constr *constr, *prev_constr;

        constr = &sum->constrs[i];
        prev_constr = &sum->constrs[i - 1];
        if (prev_constr->name == constr->name)
            gsfatal("%P: Duplicate constructor %y", pos, constr->name)
        ;
        if (strcmp(prev_constr->name->name, constr->name->name) > 0)
            gsfatal("%P: Constructors in the wrong order: %y should be after %y", pos, prev_constr->name, constr->name)
        ;
    }

    for (i = 0; i < sum->numconstrs; i++)
        if (sum->constrs[i].name == c)
            return i
    ;

    gsfatal("%P: Type %y lacks a constructor %y", pos, tyname, c);

    return -1;
}

static
void
gsbc_check_field_order(struct gspos pos, int nfields, struct gstype_field *fields)
{
    int i;

    for (i = 1; i < nfields; i ++) {
        if (fields[i - 1].name == fields[i].name)
            gsfatal("%P: Duplicate field %y", pos, fields[i].name)
        ;
        if (strcmp(fields[i - 1].name->name, fields[i].name->name) > 0)
            gsfatal("%P: Fields out of order; %y should come after %y", pos, fields[i - 1], fields[i].name)
        ;
    }
}

static
struct gstype *
gsbc_find_field_in_product(struct gspos pos, struct gstype_product *prod, gsinterned_string field)
{
    int i;

    for (i = 0; i < prod->numfields; i++) {
        if (prod->fields[i].name == field)
            return prod->fields[i].type
        ;
    }

    return 0;
}

static gsinterned_string gssymoplift, gssymopcoerce, gssymopforce, gssymopstrict, gssymopubanalyze, gssymopapp;

static
int
gsbc_typecheck_cont_push_op(struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")
        || gssymceq(p->directive, gssymopcoerce, gssymcodeop, ".coerce")
        || gssymceq(p->directive, gssymopforce, gssymcodeop, ".force")
        || gssymceq(p->directive, gssymopstrict, gssymcodeop, ".strict")
        || gssymceq(p->directive, gssymopubanalyze, gssymcodeop, ".ubanalyze")
        || gssymceq(p->directive, gssymopapp, gssymcodeop, ".app")
    ) {
        if (pcl->stacksize >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many continuations", p->pos)
        ;
        pcl->stack[pcl->stacksize++] = p;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_typecheck_cont(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gstype **pcalculated_type)
{
    int i;

    if (p->directive == gssymoplift) {
        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(*pcalculated_type), gskind_unlifted_kind());
        *pcalculated_type = gstypes_compile_lift(p->pos, *pcalculated_type);
    } else if (p->directive == gssymopcoerce) {
        int coercionreg;
        struct gstype *src_type, *dest_type;
        struct gsbc_coercion_type *coercion_type;

        coercionreg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        coercion_type = pcl->regcoerciontypes[coercionreg];
        if (!coercion_type)
            gsfatal("%P: Couldn't find type of coercion %y", p->pos, p->arguments[0])
        ;
        src_type = coercion_type->source;
        dest_type = coercion_type->dest;
        for (i = 1; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            src_type = gstype_apply(p->pos, src_type, pcl->tyregs[regarg]);
            dest_type = gstype_apply(p->pos, dest_type, pcl->tyregs[regarg]);
        }
        gstypes_type_check_type_fail(p->pos, *pcalculated_type, src_type);
        *pcalculated_type = dest_type;
    } else if (p->directive == gssymopforce) {
        struct gstype_lift *calculated_type_lift;
        int creg = 0;
        struct gsbc_code_item_type *cty;

        if ((*pcalculated_type)->node != gstype_lift)
            gsfatal("%P: The expression after .force must have a type literally of the form ⌊τ⌋ (not e.g. a lifted abstract type)", p->pos)
        ;
        calculated_type_lift = (struct gstype_lift *)*pcalculated_type;

        gsargcheck(p, 0, "code");
        creg = gsbc_find_register(p, pcl->coderegs, pcl->ncodes, p->arguments[0]);
        cty = gsbc_typecheck_copy_code_item_type(pcl->codetypes[creg]);

        if (cty->type != gsbc_code_item_force_cont)
            gsfatal("%P: continuation in .force is not a force continuation", p->pos)
        ;

        gsbc_typecheck_free_type_variables(pcl, p, cty);
        gsbc_typecheck_free_variables(pcl, p, cty);

        gstypes_type_check_type_fail(p->pos, calculated_type_lift->arg, cty->cont_arg_type);

        *pcalculated_type = cty->result_type;
    } else if (p->directive == gssymopstrict) {
        int creg = 0;
        struct gsbc_code_item_type *cty;

        gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(*pcalculated_type), gskind_lifted_kind());

        gsargcheck(p, 0, "code");
        creg = gsbc_find_register(p, pcl->coderegs, pcl->ncodes, p->arguments[0]);
        cty = gsbc_typecheck_copy_code_item_type(pcl->codetypes[creg]);

        if (cty->type != gsbc_code_item_strict_cont)
            gsfatal("%P: continuation in .strict is not a strict continuation", p->pos)
        ;

        gsbc_typecheck_free_type_variables(pcl, p, cty);
        gsbc_typecheck_free_variables(pcl, p, cty);

        gstypes_type_check_type_fail(p->pos, *pcalculated_type, cty->cont_arg_type);

        *pcalculated_type = cty->result_type;
    } else if (p->directive == gssymopubanalyze) {
        int nconstrs;
        int j;
        struct gstype_constr constrs[MAX_NUM_REGISTERS];
        struct gsbc_code_item_type *conttypes[MAX_NUM_REGISTERS];

        if ((*pcalculated_type)->node != gstype_ubsum)
            gsfatal("%P: Scrutinee of .ubanalyze not of un-boxed sum type (maybe it's lifted and you meant .ubsumforce?)", p->pos)
        ;
        nconstrs = 0;
        conttypes[0] = 0;
        for (i = 0; i < p->numarguments && p->arguments[i]->type != gssymseparator; i += 2) {
            int regcont;

            if (nconstrs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many cases in .ubanalyze", p->pos)
            ;
            constrs[nconstrs].name = p->arguments[i];
            regcont = gsbc_find_register(p, pcl->coderegs, pcl->ncodes, p->arguments[i + 1]);
            conttypes[nconstrs] = gsbc_typecheck_copy_code_item_type(pcl->codetypes[regcont]);
            nconstrs++;
        }
        if (nconstrs < 1)
            gsfatal("%P: Cannot use .ubanalyze to branch on the empty sum type; use .ubeanalyze if you have a need to do this instead", p->pos)
        ;
        for (i = 1; i < nconstrs; i++) {
            if (conttypes[0]->numftyvs != conttypes[i]->numftyvs)
                gsfatal("%P: Mis-matched number of free type variables: %y has %d but %y has %d",
                    p->pos,
                    p->arguments[2*0 + 1], conttypes[0]->numftyvs,
                    p->arguments[2*i + 1], conttypes[i]->numftyvs
                )
            ;
            for (j = 0; j < conttypes[0]->numftyvs; j++)
                if (conttypes[0]->tyfvs[j] != conttypes[i]->tyfvs[j])
                    gsfatal("%P: Mis-matched free type variable #%d: %y has %y but %y has %y",
                        p->pos, j,
                        p->arguments[2*0 + 1], conttypes[0]->tyfvs[j],
                        p->arguments[2*i + 1], conttypes[i]->tyfvs[j]
                    )
            ;
        }
        for (i = 0; i < nconstrs; i++) {
            gsbc_typecheck_free_type_variables(pcl, p, conttypes[i]);
            gsbc_typecheck_free_variables(pcl, p, conttypes[i]);

            constrs[i].argtype = conttypes[i]->cont_arg_type;
        }
        gstypes_type_check_type_fail(p->pos, *pcalculated_type, gstypes_compile_ubsumv(p->pos, nconstrs, constrs));
        *pcalculated_type = conttypes[0]->result_type;
        for (i = 1; i < nconstrs; i++) {
            if (conttypes[0]->numfvs != conttypes[i]->numfvs)
                gsfatal("%P: Mis-matched number of free variables: %y has %d but %y has %d",
                    p->pos,
                    p->arguments[2*0 + 1], conttypes[0]->numfvs,
                    p->arguments[2*i + 1], conttypes[i]->numfvs
                )
            ;
            for (j = 0; j < conttypes[0]->numfvs; j++)
                if (conttypes[0]->fvs[j] != conttypes[i]->fvs[j])
                    gsfatal("%P: Mis-matched free variable #%d: %y has %y but %y has %y",
                        p->pos, j,
                        p->arguments[2*0 + 1], conttypes[0]->fvs[j],
                        p->arguments[2*i + 1], conttypes[i]->fvs[j]
                    )
            ;
            gstypes_type_check_type_fail(p->pos, conttypes[i]->result_type, *pcalculated_type);
        }
    } else if (p->directive == gssymopapp) {
        for (i = 0; i < p->numarguments; i++) {
            int regarg;
            int lifted;

            lifted = 0;
            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            if ((*pcalculated_type)->node == gstype_lift) {
                struct gstype_lift *lift;

                lift = (struct gstype_lift *)*pcalculated_type;
                *pcalculated_type = lift->arg;
            }
            if ((*pcalculated_type)->node == gstype_fun) {
                struct gstype_fun *fun;

                fun = (struct gstype_fun *)*pcalculated_type;
                gstypes_type_check_type_fail(p->pos, pcl->regtypes[regarg], fun->tyarg);
                if (lifted)
                    gstypes_kind_check_fail(p->pos, gstypes_calculate_kind(fun->tyres), gskind_lifted_kind())
                ;
                *pcalculated_type = fun->tyres;
            } else if ((*pcalculated_type)->node == gstype_forall)
                gsfatal("%P: Too many arguments (type is polymorphic)", p->pos)
            ; else
                gsfatal("%P: Too many arguments (max %d; got %d)", p->pos, i, p->numarguments)
            ;
        }
    } else {
        return 0;
    }
    return 1;
}

struct gsbc_typecheck_impprog_closure {
    gsinterned_string primsetname, prim;
    int nbinds;
    int bind_bound[MAX_NUM_REGISTERS];
    int first_rhs_lifted;
};

static void gsbc_setup_code_impprog_closure(struct gsbc_typecheck_impprog_closure *, gsinterned_string, gsinterned_string);

static int gsbc_typecheck_bind_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_typecheck_code_or_api_expr_closure *, struct gsbc_typecheck_impprog_closure *);

static void gsbc_check_api_free_variable_decls(struct gsbc_typecheck_code_or_api_expr_closure *, struct gsparsedline *, gsinterned_string, struct gsbc_code_item_type *, int *);

static
struct gsbc_code_item_type *
gsbc_typecheck_api_expr(struct gspos pos, struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, gsinterned_string primsetname, gsinterned_string prim)
{
    static gsinterned_string gssymbody;

    struct gsbc_typecheck_code_or_api_expr_closure cl;
    struct gsbc_typecheck_impprog_closure impcl;

    struct gstype *calculated_type;

    gsbc_setup_code_expr_closure(&cl);
    gsbc_setup_code_impprog_closure(&impcl, primsetname, prim);

    while (gsbc_typecheck_code_type_gvar_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_subcode_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_arg_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_code_type_let_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_fv_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_typecheck_data_arg_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (
        gsbc_typecheck_alloc_op(symtable, p, &cl)
        || gsbc_typecheck_bind_op(symtable, p, &cl, &impcl)
    )
        p = gsinput_next_line(ppseg, p)
    ;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymceq(p->directive, gssymbody, gssymcodeop, ".body")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, cl.coderegs, cl.ncodes, p->arguments[0]);
            cty = gsbc_typecheck_copy_code_item_type(cl.codetypes[creg]);

            gsbc_typecheck_free_type_variables(&cl, p, cty);
            gsbc_typecheck_free_variables(&cl, p, cty);
            gsbc_check_api_free_variable_decls(&cl, p, p->arguments[0], cty, impcl.bind_bound);
            calculated_type = cty->result_type;

            gsbc_typecheck_check_api_statement_type(p->pos, calculated_type, primsetname, prim, impcl.nbinds == 0 ? &impcl.first_rhs_lifted : 0);
            goto have_type;
        } else {
            gsfatal(UNIMPL("%P: gsbc_typecheck_api_expr(%y)"), p->pos, p->directive);
        }
    }

have_type:

    if (impcl.first_rhs_lifted) {
        if (calculated_type->node != gstype_lift)
            calculated_type = gstypes_compile_lift(pos, calculated_type)
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Ensure API block statement has unlifted type", pos);
    }

    while (cl.stacksize) {
        p = cl.stack[--cl.stacksize];
        if (!(
            gsbc_typecheck_data_arg(&cl, p, &calculated_type)
            || gsbc_typecheck_code_type_arg(&cl, p, &calculated_type)
        ))
            gsfatal(UNIMPL("%P: Un-implemented argument op %y"), p->pos, p->directive)
        ;
    }

    return gsbc_typecheck_compile_code_item_type(gsbc_code_item_impprog, 0, calculated_type, &cl);
}

void
gsbc_setup_code_impprog_closure(struct gsbc_typecheck_impprog_closure *pimpcl, gsinterned_string primsetname, gsinterned_string prim)
{
    pimpcl->primsetname = primsetname;
    pimpcl->prim = prim;

    pimpcl->nbinds = 0;
    memset(pimpcl->bind_bound, 0, sizeof(pimpcl->bind_bound));
}

int
gsbc_typecheck_bind_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsbc_typecheck_impprog_closure *pimpcl)
{
    static gsinterned_string gssymbind;

    if (gssymceq(p->directive, gssymbind, gssymcodeop, ".bind")) {
        int creg = 0;
        struct gsbc_code_item_type *cty;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        gsargcheck(p, 0, "code");
        creg = gsbc_find_register(p, pcl->coderegs, pcl->ncodes, p->arguments[0]);
        cty = gsbc_typecheck_copy_code_item_type(pcl->codetypes[creg]);

        gsbc_typecheck_free_type_variables(pcl, p, cty);
        gsbc_typecheck_free_variables(pcl, p, cty);
        gsbc_check_api_free_variable_decls(pcl, p, p->arguments[0], cty, pimpcl->bind_bound);

        pimpcl->bind_bound[pcl->nregs] = 1;
        pcl->regs[pcl->nregs] = p->label;
        pcl->regtypes[pcl->nregs] = gsbc_typecheck_check_api_statement_type(p->pos, cty->result_type, pimpcl->primsetname, pimpcl->prim, pimpcl->nbinds == 0 ? &pimpcl->first_rhs_lifted : 0);

        pcl->nregs++;
        pimpcl->nbinds++;
    } else {
        return 0;
    }
    return 1;
}

static struct gsbc_code_item_type *gsbc_typecheck_alloc_code_item_type(int, int);

static
struct gsbc_code_item_type *
gsbc_typecheck_compile_code_item_type(int type, struct gstype *cont_arg_type, struct gstype *calculated_type, struct gsbc_typecheck_code_or_api_expr_closure *pcl)
{
    struct gsbc_code_item_type *res;
    int i;

    res = gsbc_typecheck_alloc_code_item_type(pcl->ntyfvs, pcl->nfvs);

    res->type = type;

    res->numftyvs = pcl->ntyfvs;
    for (i = 0; i < pcl->ntyfvs; i++) {
        res->tyfvs[i] = pcl->tyfvnames[i];
        res->tyfvkinds[i] = pcl->tyfvkinds[i];
    }

    res->numfvs = pcl->nfvs;
    for (i = 0; i < pcl->nfvs; i++) {
        res->fvs[i] = pcl->fvnames[i];
        res->fvtypes[i] = pcl->fvtypes[i];
        res->efv_bound[i] = pcl->efv_bound[i];
    }

    res->cont_arg_type = cont_arg_type;
    res->result_type = calculated_type;

    return res;
}

struct gsbc_code_item_type *
gsbc_typecheck_copy_code_item_type(struct gsbc_code_item_type *cty)
{
    struct gsbc_code_item_type *res;
    int i;

    res = gsbc_typecheck_alloc_code_item_type(cty->numftyvs, cty->numfvs);

    res->type = cty->type;

    res->numftyvs = cty->numftyvs;
    for (i = 0; i < cty->numftyvs; i++) {
        res->tyfvs[i] = cty->tyfvs[i];
        res->tyfvkinds[i] = cty->tyfvkinds[i];
    }

    res->numfvs = cty->numfvs;
    for (i = 0; i < cty->numfvs; i++) {
        res->fvs[i] = cty->fvs[i];
        res->efv_bound[i] = cty->efv_bound[i];
        res->fvtypes[i] = cty->fvtypes[i];
    }

    res->cont_arg_type = cty->cont_arg_type;
    res->result_type = cty->result_type;

    return res;
}

static
struct gsbc_code_item_type *
gsbc_typecheck_alloc_code_item_type(int ntyfvs, int nfvs)
{
    struct gsbc_code_item_type *res;
    ulong end_of_res, end_of_tyfvs, end_of_tyfvkinds, end_of_fvs, end_of_fvtypes, end_of_efv_bound;

    end_of_res = sizeof(*res);
    end_of_tyfvs = end_of_res + ntyfvs * sizeof(gsinterned_string);
    end_of_tyfvkinds = end_of_tyfvs + ntyfvs * sizeof(struct gskind *);
    end_of_fvs = end_of_tyfvkinds + nfvs * sizeof(gsinterned_string);
    end_of_fvtypes = end_of_fvs + nfvs * sizeof(struct gstype *);
    end_of_efv_bound = end_of_fvtypes + nfvs * sizeof(int);

    res = gs_sys_global_block_suballoc(&gsbc_code_type_info, end_of_efv_bound);
    res->tyfvs = (gsinterned_string*)((uchar*)res + end_of_res);
    res->tyfvkinds = (struct gskind **)((uchar*)res + end_of_tyfvs);
    res->fvs = (gsinterned_string*)((uchar*)res + end_of_tyfvkinds);
    res->fvtypes = (struct gstype **)((uchar*)res + end_of_fvs);
    res->efv_bound = (int*)((uchar*)res + end_of_fvtypes);

    return res;
}

int
gsbc_typecheck_data_arg(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gstype **pcalculated_type)
{
    struct gstype *ty;

    if (p->directive == gssymlarg || p->directive == gssymarg) {
        if (!pcl->nargs--) gsfatal(UNIMPL("%P: Error: didn't put this argument onto list of arguments"), p->pos);

        ty = pcl->argtypes[pcl->nargs];

        *pcalculated_type = gstypes_compile_fun(p->pos, ty, *pcalculated_type);
        if (p->directive == gssymlarg)
            *pcalculated_type = gstypes_compile_lift(p->pos, *pcalculated_type)
        ;
    } else {
        return 0;
    }
    return 1;
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
        } else if (!strcmp("\"uΣ〈", tok)) {
            int nconstrs;
            struct gstype_constr constrs[MAX_NUM_REGISTERS];

            nconstrs = 0;
            while (tok = s, s = gsbc_typecheck_compile_prim_type_skip_token(s), strcmp("〉", tok)) {
                if (nconstrs >= MAX_NUM_REGISTERS)
                    gsfatal("%P: Too many constrs in internal primtype; max 0x%x", pos, MAX_NUM_REGISTERS)
                ;
                constrs[nconstrs++].name = gsintern_string(gssymconstrlable, tok);
            }
            if (stacksize < nconstrs)
                gsfatal("%P: Stack underflow in internal primtype", pos)
            ;
            for (i = 0; i < nconstrs; i++)
                constrs[i].argtype = stack[stacksize - nconstrs + i]
            ;
            stacksize -= nconstrs;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal(UNIMPL("%P: Stack overflow in internal primtype; max 0x%x"), pos, MAX_NUM_REGISTERS)
            ;

            stack[stacksize++] = gstypes_compile_ubsumv(pos, nconstrs, constrs);
        } else if (!strcmp("\"Π〈", tok)) {
            int nfields;
            struct gstype_field fields[MAX_NUM_REGISTERS];

            nfields = 0;
            while (tok = s, s = gsbc_typecheck_compile_prim_type_skip_token(s), strcmp("〉", tok)) {
                if (nfields >= MAX_NUM_REGISTERS)
                    gsfatal(UNIMPL("%P: Too many fields in internal primtype; max 0x%x"), pos, MAX_NUM_REGISTERS)
                ;
                fields[nfields++].name = gsintern_string(gssymfieldlable, tok);
            }
            if (stacksize < nfields)
                gsfatal("%P: Stack underflow in internal primtype", pos)
            ;
            for (i = 0; i < nfields; i++)
                fields[i].type = stack[stacksize - nfields + i]
            ;
            stacksize -= nfields;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal(UNIMPL("%P: Stack overflow in internal primtype; max 0x%x"), pos, MAX_NUM_REGISTERS)
            ;

            stack[stacksize++] = gstypes_compile_productv(pos, nfields, fields);
        } else if (!strcmp("\"uΠ〈", tok)) {
            int nfields;
            struct gstype_field fields[MAX_NUM_REGISTERS];

            nfields = 0;
            while (tok = s, s = gsbc_typecheck_compile_prim_type_skip_token(s), strcmp("〉", tok)) {
                if (nfields >= MAX_NUM_REGISTERS)
                    gsfatal(UNIMPL("%P: Too many fields in internal primtype; max 0x%x"), pos, MAX_NUM_REGISTERS)
                ;
                fields[nfields++].name = gsintern_string(gssymfieldlable, tok);
            }
            if (stacksize < nfields)
                gsfatal("%P: Stack underflow in internal primtype", pos)
            ;
            for (i = 0; i < nfields; i++)
                fields[i].type = stack[stacksize - nfields + i]
            ;
            stacksize -= nfields;

            if (stacksize >= MAX_NUM_REGISTERS)
                gsfatal(UNIMPL("%P: Stack overflow in internal primtype; max 0x%x"), pos, MAX_NUM_REGISTERS)
            ;

            stack[stacksize++] = gstypes_compile_ubproductv(pos, nfields, fields);
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
void
gsbc_typecheck_validate_prim_type(struct gspos pos, gsinterned_string primsetname, struct gstype *type)
{
    while (type->node == gstype_app) {
        struct gstype_app *app = (struct gstype_app *)type;
        type = app->fun;
    }
    switch (type->node) {
        case gstype_forall: {
            struct gstype_forall *forall = (struct gstype_forall *)type;
            gsbc_typecheck_validate_prim_type(pos, primsetname, forall->body);
            return;
        }
        case gstype_fun: {
            struct gstype_fun *fun = (struct gstype_fun *)type;
            struct gstype *arg = fun->tyarg;
            while (arg->node == gstype_app) {
                struct gstype_app *app = (struct gstype_app *)arg;
                arg = app->fun;
            }
            switch (arg->node) {
                case gstype_knprim: {
                    struct gstype_knprim *prim = (struct gstype_knprim *)arg;
                    if (strcmp(prim->primset->name, primsetname->name))
                        break
                    ;
                    switch (prim->primtypegroup) {
                        case gsprim_type_defined:
                        case gsprim_type_elim:
                            return;
                        default:
                            gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(fun; arg prim group type = %d)"), pos, prim->primtypegroup);
                    }
                }
                case gstype_unprim: {
                    struct gstype_unprim *prim = (struct gstype_unprim *)arg;
                    if (prim->primsetname != primsetname)
                        break
                    ;
                    switch (prim->primtypegroup) {
                        case gsprim_type_elim:
                            return;
                        default:
                            gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(fun; arg prim group type = %d)"), pos, prim->primtypegroup);
                    }
                }
                case gstype_lift:
                    break;
                default:
                    gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(fun; arg node = %d)"), pos, arg->node);
            }
            gsbc_typecheck_validate_prim_type(pos, primsetname, fun->tyres);
            return;
        }
        case gstype_knprim: {
            struct gstype_knprim *prim = (struct gstype_knprim *)type;
            if (strcmp(prim->primset->name, primsetname->name))
                gsfatal("%P: Invalid primitive type")
            ;
            switch (prim->primtypegroup) {
                case gsprim_type_intr:
                    return;
                default:
                    gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(fun; result prim group type = %d)"), pos, prim->primtypegroup);
            }
        }
        case gstype_unprim: {
            struct gstype_unprim *prim = (struct gstype_unprim *)type;
            if (prim->primsetname != primsetname)
                gsfatal("%P: Invalid primitive type")
            ;
            switch (prim->primtypegroup) {
                case gsprim_type_intr:
                    return;
                default:
                    gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(fun; result prim group type = %d)"), pos, prim->primtypegroup);
            }
        }
        default:
            gsfatal(UNIMPL("%P: gsbc_typecheck_validate_prim_type(node = %d)"), pos, type->node);
    }
}

static
void
gsbc_typecheck_free_type_variables(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gsbc_code_item_type *cty)
{
    int i, j;
    gsinterned_string ftyvs[MAX_NUM_REGISTERS];

    for (i = 0; i < cty->numftyvs; i++)
        ftyvs[i] = cty->tyfvs[i]
    ;

    for (i = 0; i < cty->numftyvs; i++) {
        int regarg;
        struct gstype *fvval;

        regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, ftyvs[i]);
        fvval = pcl->tyregs[regarg];
        gstypes_kind_check_fail(p->pos, pcl->tyregkinds[regarg], cty->tyfvkinds[i]);
        for (j = i + 1; j < cty->numftyvs; j++) {
            if (gstypes_is_ftyvar(cty->tyfvs[j], fvval))
                gsfatal(UNIMPL("%P: α-rename other free type variables"), p->pos)
            ;
        }
        for (j = 0; j < cty->numfvs; j++)
            cty->fvtypes[j] = gstypes_subst(p->pos, cty->fvtypes[j], cty->tyfvs[i], fvval)
        ;
        if (cty->cont_arg_type)
            cty->cont_arg_type = gstypes_subst(p->pos, cty->cont_arg_type, cty->tyfvs[i], fvval)
        ;
        cty->result_type = gstypes_subst(p->pos, cty->result_type, cty->tyfvs[i], fvval);
    }
}

static
void
gsbc_typecheck_free_variables(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, struct gsbc_code_item_type *cty)
{
    int i;

    for (i = 0; i < cty->numfvs; i++) {
        int fvreg;

        fvreg = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i]);
        gstypes_type_check_type_fail(p->pos, pcl->regtypes[fvreg], cty->fvtypes[i]);
    }
}

static
void
gsbc_check_api_free_variable_decls(struct gsbc_typecheck_code_or_api_expr_closure *pcl, struct gsparsedline *p, gsinterned_string codelabel, struct gsbc_code_item_type *cty, int *bind_bound)
{
    int i;

    for (i = 0; i < cty->numfvs; i++) {
        int fvreg;

        fvreg = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i]);
        if (bind_bound[fvreg]) {
            if (!cty->efv_bound[i]) {
                struct gskind *kind;

                kind = gstypes_calculate_kind(cty->fvtypes[i]);
                if (kind->node != gskind_lifted)
                    gsfatal("%P: '%y' is .bind-bound but passed for a .fv-bound variable (should be .efv-bound)", p->pos, cty->fvs[i])
                ;
            }
        }
    }
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
    struct gsstringbuilder *err;

    err = gsreserve_string_builder();
    if (gstypes_type_check(err, pos, pactual, pexpected) < 0) {
        gsfinish_string_builder(err);
        gsfatal("%s", err->start);
    }
    gsfinish_string_builder(err);
}

int
gstypes_type_check(struct gsstringbuilder *err, struct gspos pos, struct gstype *pactual, struct gstype *pexpected)
{
    char actual_buf[0x400];
    char expected_buf[0x400];
    int i;

    if (gstypes_eprint_type(actual_buf, actual_buf + sizeof(actual_buf), pactual) >= actual_buf + sizeof(actual_buf)) {
        gsstring_builder_print(err, UNIMPL("%P: buffer overflow printing actual type %P"), pos, pactual->pos);
        return -1;
    }
    if (gstypes_eprint_type(expected_buf, expected_buf + sizeof(expected_buf), pexpected) >= actual_buf + sizeof(actual_buf)) {
        gsstring_builder_print(err, UNIMPL("%P: buffer overflow printing actual type %P"), pos, pactual->pos);
        return -1;
    }

    if (pactual->node != pexpected->node) {
        gsstring_builder_print(err, "%P: I don't think %s (%P) is %s (%P)", pos, actual_buf, pactual->pos, expected_buf, pexpected->pos);
        return -1;
    }

    switch (pexpected->node) {
        case gstype_abstract: {
            struct gstype_abstract *pactual_abstract, *pexpected_abstract;

            pactual_abstract = (struct gstype_abstract *)pactual;
            pexpected_abstract = (struct gstype_abstract *)pexpected;

            if (pactual_abstract->name != pexpected_abstract->name) {
                gsstring_builder_print(err, "%P: I don't think abstype %y is the same as abstype %y", pos, pactual_abstract->name, pexpected_abstract->name);
                return -1;
            }
            return 0;
        }
        case gstype_knprim: {
            struct gstype_knprim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_knprim *)pactual;
            pexpected_prim = (struct gstype_knprim *)pexpected;

            if (pactual_prim->primset != pexpected_prim->primset) {
                gsstring_builder_print(err, "%P: I don't think primset %s is the same as primset %s", pos, pactual_prim->primset->name, pexpected_prim->primset->name);
                return -1;
            }
            if (pactual_prim->primname != pexpected_prim->primname) {
                gsstring_builder_print(err, "%P: I don't think prim %s is the same as prim %s", pos, pactual_prim->primname->name, pexpected_prim->primname->name);
                return -1;
            }
            return 0;
        }
        case gstype_unprim: {
            struct gstype_unprim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_unprim *)pactual;
            pexpected_prim = (struct gstype_unprim *)pexpected;

            if (pactual_prim->primsetname != pexpected_prim->primsetname) {
                gsstring_builder_print(err, "%P: I don't think primset %s is the same as primset %s", pos, pactual_prim->primsetname->name, pexpected_prim->primsetname->name);
                return -1;
            }
            if (pactual_prim->primname != pexpected_prim->primname) {
                gsstring_builder_print(err, "%P: I don't think prim %s is the same as prim %s", pos, pactual_prim->primname->name, pexpected_prim->primname->name);
                return -1;
            }
            return 0;
        }
        case gstype_var: {
            struct gstype_var *pactual_var, *pexpected_var;

            pactual_var = (struct gstype_var *)pactual;
            pexpected_var = (struct gstype_var *)pexpected;

            if (pactual_var->name != pexpected_var->name) {
                gsstring_builder_print(err, "%P: I don't think %y is the same as %y", pos, pactual_var->name, pexpected_var->name);
                return -1;
            }
            return 0;
        }
        case gstype_lambda: {
            struct gstype_lambda *pactual_lambda, *pexpected_lambda;
            struct gstype *actual_renamed_body, *expected_renamed_body;
            char nm[0x100];
            int n;
            gsinterned_string var;
            struct gstype *varty;

            pactual_lambda = (struct gstype_lambda *)pactual;
            pexpected_lambda = (struct gstype_lambda *)pexpected;

            if (gstypes_kind_check(pos, pactual_lambda->kind, pexpected_lambda->kind, err) < 0)
                return -1
            ;

            n = 0;
            do {
                if (seprint(nm, nm + sizeof(nm), "%y%d", pexpected_lambda->var, n++) >= nm + sizeof(nm)) {
                    gsstring_builder_print(err, UNIMPL("%P: Buffer overflow during α-renaming"), pexpected->pos);
                    return -1;
                }
                var = gsintern_string(gssymtypelable, nm);
            } while (gstypes_is_ftyvar(var, pactual_lambda->body) || gstypes_is_ftyvar(var, pexpected_lambda->body));
            varty = gstypes_compile_type_var(pos, var, pexpected_lambda->kind);
            actual_renamed_body = gstypes_subst(pos, pactual_lambda->body, pactual_lambda->var, varty);
            expected_renamed_body = gstypes_subst(pos, pexpected_lambda->body, pexpected_lambda->var, varty);

            return gstypes_type_check(err, pos, actual_renamed_body, expected_renamed_body);
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

            if (gstypes_kind_check(pos, pactual_forall->kind, pexpected_forall->kind, err) < 0)
                return -1
            ;

            n = 0;
            do {
                if (seprint(nm, nm + sizeof(nm), "%y%d", pexpected_forall->var, n++) >= nm + sizeof(nm)) {
                    gsstring_builder_print(err, UNIMPL("%P: Buffer overflow during α-renaming"), pexpected->pos);
                    return -1;
                }
                var = gsintern_string(gssymtypelable, nm);
            } while (gstypes_is_ftyvar(var, pactual_forall->body) || gstypes_is_ftyvar(var, pexpected_forall->body));
            varty = gstypes_compile_type_var(pos, var, pactual_forall->kind);
            actual_renamed_body = gstypes_subst(pos, pactual_forall->body, pactual_forall->var, varty);
            expected_renamed_body = gstypes_subst(pos, pexpected_forall->body, pexpected_forall->var, varty);

            return gstypes_type_check(err, pos, actual_renamed_body, expected_renamed_body);
        }
        case gstype_exists: {
            struct gstype_exists *pactual_exists, *pexpected_exists;
            struct gstype *actual_renamed_body, *expected_renamed_body;
            char nm[0x100];
            int n;
            gsinterned_string var;
            struct gstype *varty;

            pactual_exists = (struct gstype_exists *)pactual;
            pexpected_exists = (struct gstype_exists *)pexpected;

            if (gstypes_kind_check(pos, pactual_exists->kind, pexpected_exists->kind, err) < 0)
                return -1
            ;

            n = 0;
            do {
                if (seprint(nm, nm + sizeof(nm), "%y%d", pexpected_exists->var, n++) >= nm + sizeof(nm)) {
                    gsstring_builder_print(err, UNIMPL("%P: Buffer overflow during α-renaming"), pexpected->pos);
                    return -1;
                }
                var = gsintern_string(gssymtypelable, nm);
            } while (gstypes_is_ftyvar(var, pactual_exists->body) || gstypes_is_ftyvar(var, pexpected_exists->body));
            varty = gstypes_compile_type_var(pos, var, pactual_exists->kind);
            actual_renamed_body = gstypes_subst(pos, pactual_exists->body, pactual_exists->var, varty);
            expected_renamed_body = gstypes_subst(pos, pexpected_exists->body, pexpected_exists->var, varty);

            return gstypes_type_check(err, pos, actual_renamed_body, expected_renamed_body);
        }
        case gstype_lift: {
            struct gstype_lift *pactual_lift, *pexpected_lift;

            pactual_lift = (struct gstype_lift *)pactual;
            pexpected_lift = (struct gstype_lift *)pexpected;

            return gstypes_type_check(err, pos, pactual_lift->arg, pexpected_lift->arg);
        }
        case gstype_app: {
            struct gstype_app *pactual_app, *pexpected_app;

            pactual_app = (struct gstype_app *)pactual;
            pexpected_app = (struct gstype_app *)pexpected;

            if (gstypes_type_check(err, pos, pactual_app->fun, pexpected_app->fun) < 0)
                return -1;
            if (gstypes_type_check(err, pos, pactual_app->arg, pexpected_app->arg) < 0)
                return -1;
            return 0;
        }
        case gstype_fun: {
            struct gstype_fun *pactual_fun, *pexpected_fun;

            pactual_fun = (struct gstype_fun *)pactual;
            pexpected_fun = (struct gstype_fun *)pexpected;

            if (gstypes_type_check(err, pos, pactual_fun->tyarg, pexpected_fun->tyarg) < 0)
                return -1;
            if (gstypes_type_check(err, pos, pactual_fun->tyres, pexpected_fun->tyres) < 0)
                return -1;
            return 0;
        }
        case gstype_sum: {
            struct gstype_sum *pactual_sum, *pexpected_sum;

            pactual_sum = (struct gstype_sum *)pactual;
            pexpected_sum = (struct gstype_sum *)pexpected;

            if (pactual_sum->numconstrs != pexpected_sum->numconstrs) {
                gsstring_builder_print(err, "%P: I don't think %s is the same as %s; they have different numbers of constrs", pos, actual_buf, expected_buf);
                return -1;
            }
            for (i = 0; i < pexpected_sum->numconstrs; i++) {
                if (pactual_sum->constrs[i].name != pexpected_sum->constrs[i].name) {
                    gsstring_builder_print(err, "%P: I don't think constructor %y (#%d), is the same as %y", pos, pactual_sum->constrs[i].name, i, pexpected_sum->constrs[i].name);
                    return -1;
                }
                if (gstypes_type_check(err, pos, pactual_sum->constrs[i].argtype, pexpected_sum->constrs[i].argtype) < 0)
                    return -1
                ;
            }
            return 0;
        }
        case gstype_ubsum: {
            struct gstype_ubsum *pactual_sum, *pexpected_sum;

            pactual_sum = (struct gstype_ubsum *)pactual;
            pexpected_sum = (struct gstype_ubsum *)pexpected;

            if (pactual_sum->numconstrs != pexpected_sum->numconstrs) {
                gsstring_builder_print(err, "%P: I don't think %s is the same as %s; they have different numbers of constrs", pos, actual_buf, expected_buf);
                return -1;
            }
            for (i = 0; i < pexpected_sum->numconstrs; i++) {
                if (pactual_sum->constrs[i].name != pexpected_sum->constrs[i].name) {
                    gsstring_builder_print(err, "%P: I don't think constructor %y (#%d), is the same as %y", pos, pactual_sum->constrs[i].name, i, pexpected_sum->constrs[i].name);
                    return -1;
                }
                if (gstypes_type_check(err, pos, pactual_sum->constrs[i].argtype, pexpected_sum->constrs[i].argtype) < 0)
                    return -1
                ;
            }
            return 0;
        }
        case gstype_product: {
            struct gstype_product *pactual_product, *pexpected_product;

            pactual_product = (struct gstype_product *)pactual;
            pexpected_product = (struct gstype_product *)pexpected;

            if (pactual_product->numfields != pexpected_product->numfields) {
                gsstring_builder_print(err, "%P: I don't think %s is the same as %s; they have different numbers of fields", pos, actual_buf, expected_buf);
                return -1;
            }
            for (i = 0; i < pexpected_product->numfields; i++) {
                if (pactual_product->fields[i].name != pexpected_product->fields[i].name) {
                    gsstring_builder_print(err, "%P: I don't think field %y is the same as %y", pos, pactual_product->fields[i].name, pexpected_product->fields[i].name);
                    return -1;
                }
                if (gstypes_type_check(err, pos, pactual_product->fields[i].type, pexpected_product->fields[i].type) < 0)
                    return -1
                ;
            }
            return 0;
        }
        case gstype_ubproduct: {
            struct gstype_ubproduct *pactual_product, *pexpected_product;

            pactual_product = (struct gstype_ubproduct *)pactual;
            pexpected_product = (struct gstype_ubproduct *)pexpected;

            if (pactual_product->numfields != pexpected_product->numfields) {
                gsstring_builder_print(err, "%P: I don't think %s is the same as %s; they have different numbers of fields", pos, actual_buf, expected_buf);
                return -1;
            }
            for (i = 0; i < pexpected_product->numfields; i++) {
                if (pactual_product->fields[i].name != pexpected_product->fields[i].name) {
                    gsstring_builder_print(err, "%P: I don't think field %y is the same as %y", pos, pactual_product->fields[i].name, pexpected_product->fields[i].name);
                    return -1;
                }
                if (gstypes_type_check(err, pos, pactual_product->fields[i].type, pexpected_product->fields[i].type) < 0)
                    return -1
                ;
            }
            return 0;
        }
        default:
            gsstring_builder_print(err, UNIMPL("gstypes_check_type(node = %d) %P: %P"), pexpected->node, pos, pexpected->pos);
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
                case gsprim_type_intr:
                    res = seprint(res, eob, "\"intrprim ");
                    break;
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
            res = seprint(res, eob, "%s ", prim->primset->name);
            res = seprint(res, eob, "%s", prim->primname->name);
            return res;
        }
        case gstype_unprim: {
            struct gstype_unprim *prim;

            prim = (struct gstype_unprim *)pty;

            switch (prim->primtypegroup) {
                case gsprim_type_defined:
                    res = seprint(res, eob, "\"definedprim ");
                    break;
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
        case gstype_lambda: {
            struct gstype_lambda *lambda;

            lambda = (struct gstype_lambda *)pty;

            res = seprint(res, eob, "(λ %y :: ", lambda->var);
            res = gstypes_eprint_kind(res, eob, lambda->kind, 0);
            res = seprint(res, eob, ". ");
            res = gstypes_eprint_type(res, eob, lambda->body);
            res = seprint(res, eob, ")");
            return res;
        }
        case gstype_forall: {
            struct gstype_forall *forall;

            forall = (struct gstype_forall *)pty;

            res = seprint(res, eob, "(∀ %y :: ", forall->var);
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
                if (i == 0)
                    res = seprint(res, eob, " ")
                ;
                res = seprint(res, eob, "%y ", sum->constrs[i].name);
                res = gstypes_eprint_type(res, eob, sum->constrs[i].argtype);
                res = seprint(res, eob, "; ");
            }
            res = seprint(res, eob, "〉");
            return res;
        }
        case gstype_ubsum: {
            struct gstype_ubsum *sum;

            sum = (struct gstype_ubsum *)pty;

            res = seprint(res, eob, "\"uΣ〈");
            for (i = 0; i < sum->numconstrs; i++) {
                if (i == 0)
                    res = seprint(res, eob, " ")
                ;
                res = seprint(res, eob, "%y ", sum->constrs[i].name);
                res = gstypes_eprint_type(res, eob, sum->constrs[i].argtype);
                res = seprint(res, eob, "; ");
            }
            res = seprint(res, eob, "〉");
            return res;
        }
        case gstype_product: {
            struct gstype_product *product;

            product = (struct gstype_product *)pty;

            res = seprint(res, eob, "\"Π〈");
            for (i = 0; i < product->numfields; i++) {
                if (i == 0)
                    res = seprint(res, eob, " ")
                ;
                res = seprint(res, eob, "%y :: ", product->fields[i].name);
                res = gstypes_eprint_type(res, eob, product->fields[i].type);
                res = seprint(res, eob, "; ");
            }
            res = seprint(res, eob, "〉");
            return res;
        }
        case gstype_ubproduct: {
            struct gstype_ubproduct *product;

            product = (struct gstype_ubproduct *)pty;

            res = seprint(res, eob, "\"uΠ〈");
            for (i = 0; i < product->numfields; i++) {
                if (i == 0)
                    res = seprint(res, eob, " ")
                ;
                res = seprint(res, eob, "%y :: ", product->fields[i].name);
                res = gstypes_eprint_type(res, eob, product->fields[i].type);
                res = seprint(res, eob, "; ");
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
    static gsinterned_string gssymtycoercion;

    struct gsparsedline *pcoercion;
    struct gsparsedfile_segment *pseg;

    pseg = items[i].pseg;
    pcoercion = items[i].v;
    if (gssymceq(pcoercion->directive, gssymtycoercion, gssymcoerciondirective, ".tycoercion")) {
        struct gsbc_coercion_type *type;

        type = gsbc_typecheck_coercion_expr(symtable, &pseg, gsinput_next_line(&pseg, pcoercion));
        gssymtable_set_coercion_type(symtable, pcoercion->label, type);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_type_check_coercion_item(%s)", pcoercion->pos, pcoercion->directive->name);
    }
}

static struct gs_sys_global_block_suballoc_info gsbc_coercion_type_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Coercion types",
    },
};

static
struct gsbc_coercion_type *
gsbc_typecheck_coercion_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    static gsinterned_string gssymtygvar, gssymtylambda, gssymtyinvert, gssymtydefinition;

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
        if (gssymceq(p->directive, gssymtygvar, gssymcoercionop, ".tygvar")) {
            if (regtype > rttygvar)
                gsfatal("%P: Too late to add type global variables", p->pos)
            ;
            regtype = rttygvar;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers", p->pos)
            ;
            regs[nregs] = p->label;
            regtypes[nregs] = gssymtable_get_type(symtable, p->label);
            if (!regtypes[nregs])
                gsfatal("%P: Couldn't find type for global %y", p->pos, p->label)
            ;
            globaldefns[nglobals] = gssymtable_get_abstype(symtable, p->label);
            nregs++;
            nglobals++;
        } else if (gssymceq(p->directive, gssymtylambda, gssymcoercionop, ".tylambda")) {
            struct gskind *kind;

            if (regtype > rttyarg)
                gsfatal("%P: Too late to add type arguments", p->pos)
            ;
            regtype = rttyarg;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers", p->pos)
            ;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "kind");
            kind = gskind_compile(p->pos, p->arguments[0]);
            regtypes[nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
            argkinds[nargs] = kind;
            arglines[nargs] = p;
            nregs++;
            nargs++;
        } else if (gssymceq(p->directive, gssymtyinvert, gssymcoercionop, ".tyinvert")) {
            regtype = rtcont;
            if (nconts >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many continuations", p->pos)
            ;
            conts[nconts] = p;
            nconts++;
        } else if (gssymceq(p->directive, gssymtydefinition, gssymcoercionop, ".tydefinition")) {
            int reg, global;

            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            if (reg >= nglobals)
                gsfatal("%P: Register %y isn't a global; can't really cast to/from definition of an abstract type unless we know what that definition is", p->arguments[0])
            ;
            global = reg;
            if (!regtypes[reg])
                gsfatal("%P: Register %y doesn't seem to be a type variable", p->pos, p->arguments[0])
            ;
            dest = regtypes[global];

            if (!globaldefns[global])
                gsfatal("%P: Register %y doesn't seem to be an abstract type", p->pos, p->arguments[0])
            ;
            source = globaldefns[global];

            for (i = 1; i < p->numarguments; i++) {
                reg = gsbc_find_register(p, regs, nregs, p->arguments[i]);
                if (!regtypes[reg])
                    gsfatal("%P: Register %y doesn't seem to be a type variable", p->pos, p->arguments[i])
                ;
                source = gstype_apply(p->pos, source, regtypes[reg]);
                dest = gstype_apply(p->pos, dest, regtypes[reg]);
            }

            if (source->node == gstype_lambda)
                gsfatal("%P: Not enough arguments to definition of %y", p->pos, p->arguments[0])
            ;

            goto have_type;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_typecheck_coercion_expr(%s)", p->pos, p->directive->name);
        }
    }

have_type:

    while (nconts--) {
        p = conts[nconts];

        if (gssymceq(p->directive, gssymtyinvert, gssymcoercionop, ".tyinvert")) {
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

    res = gs_sys_global_block_suballoc(&gsbc_coercion_type_info, sizeof(*res));
    res->source = source;
    res->dest = dest;

    return res;
}
