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

                        kinds[i] = gskind_compile(ptype, ptype->arguments[0]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyexpr")) {
                        if (ptype->numarguments > 0) {
                            kinds[i] = gskind_compile(ptype, ptype->arguments[0]);

                            gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                        } else {
                            kinds[i] = 0;
                        }
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tydefinedprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyapiprim")) {
                        gsargcheck(ptype, 2, "Kind");

                        kinds[i] = gskind_compile(ptype, ptype->arguments[2]);

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else {
                        gsfatal("%s:%d: %s:%d: gstypes_process_type_declarations(%s) next", __FILE__, __LINE__, ptype->pos.file->name, ptype->pos.lineno, ptype->directive->name);
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
                struct gspos pos;

                pos.file = type->file;
                pos.lineno = type->lineno;
                gstypes_kind_check(pos, calculated_kind, kinds[i]);
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
            gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v, "gstypes_kind_check_item(type = %d)", items[i].type);
    }
}

static void gstypes_kind_check_simple(gsinterned_string, int, struct gskind *);

struct gskind *
gstypes_calculate_kind(struct gstype *type)
{
    int i;

    switch (type->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)type;
            return gstypes_calculate_kind(indir->referent);
        }
        case gstype_abstract: {
            struct gstype_abstract *abs;

            abs = (struct gstype_abstract *)type;
            if (abs->kind)
                return abs->kind;
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "Abstract types without declared kinds");
        }
        case gstype_prim: {
            struct gstype_prim *prim;
            struct gsregistered_primtype *primtype;

            prim = (struct gstype_prim *)type;
            if (!prim->primset)
                if (!prim->kind)
                    gsfatal("%s:%d: Primitive %s seems to lack a declared kind", type->file->name, type->lineno, prim->name->name);
                else
                    return prim->kind
                ;
            if (!(primtype = gsprims_lookup_type(prim->primset, prim->name->name)))
                gsfatal("%s:%d: Primset %s lacks a type member %s",
                    type->file->name,
                    type->lineno,
                    prim->primset->name,
                    prim->name->name
                )
            ;
            if (!primtype->kind)
                gsfatal_unimpl(__FILE__, __LINE__, "Panic! Primitype type %s (%s:%d) lacks a declared kind", primtype->name, primtype->file, primtype->line);
            return gstypes_compile_prim_kind(primtype->kind);
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
            gstypes_kind_check_simple(type->file, type->lineno, kybody);
            return kybody;
        }
        case gstype_lift: {
            struct gstype_lift *lift;
            struct gskind *kyarg;
            struct gspos pos;

            lift = (struct gstype_lift *)type;

            pos.file = type->file;
            pos.lineno = type->lineno;

            kyarg = gstypes_calculate_kind(lift->arg);
            gstypes_kind_check(pos, kyarg, gskind_unlifted_kind());

            return gskind_lifted_kind();
        }
        case gstype_app: {
            struct gstype_app *app;
            struct gskind *funkind, *argkind;
            struct gspos pos;

            app = (struct gstype_app *)type;
            funkind = gstypes_calculate_kind(app->fun);
            argkind = gstypes_calculate_kind(app->arg);

            pos.file = type->file;
            pos.lineno = type->lineno;

            switch (funkind->node) {
                case gskind_exponential:
                    gstypes_kind_check(pos, argkind, funkind->args[1]);
                    return funkind->args[0];
                case gskind_lifted:
                    gsfatal_bad_type(type->file, type->lineno, type, "Wrong kind: Expected ^, got *");
                default:
                    gsfatal_unimpl_type(__FILE__, __LINE__, type, "'function' kind (node = %d)", funkind->node);
            }
        }
        case gstype_sum: {
            struct gstype_sum *sum;

            sum = (struct gstype_sum *)type;

            for (i = 0; i < sum->numconstrs; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "check kind of constr arg type");
            }

            return gskind_unlifted_kind();
        }
        case gstype_product: {
            struct gstype_product *prod;

            prod = (struct gstype_product *)type;

            for (i = 0; i < prod->numfields; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "check kind of field type");
            }

            return gskind_unlifted_kind();
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "gstypes_calculate_kind(node = %d)", type->node);
    }
    return 0;
}

static char *seprint_kind_name(char *, char *, struct gskind *);

void
gstypes_kind_check(struct gspos pos, struct gskind *kyactual, struct gskind *kyexpected)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

    switch (kyexpected->node) {
        case gskind_unknown:
            if (kyactual->node != gskind_unknown && kyactual->node != gskind_unlifted && kyactual->node != gskind_lifted)
                gsfatal("%P: Incorrect kind: Expected '?'; got '%s'", pos, actual_name);
            return;
        case gskind_unlifted:
            if (kyactual->node != gskind_unlifted)
                gsfatal("%P: Incorrect kind: Expected 'u'; got '%s'", pos, actual_name);
            return;
        case gskind_lifted:
            if (kyactual->node != gskind_lifted)
                gsfatal("%P: Incorrect kind: Expected '*'; got '%s'", pos, actual_name);
            return;
        case gskind_exponential:
            if (kyactual->node != gskind_exponential)
                gsfatal("%P: Incorrect kind: Expected '^'; got '%s'", pos, actual_name);
            gstypes_kind_check(pos, kyactual->args[0], kyexpected->args[0]);
            gstypes_kind_check(pos, kyactual->args[1], kyexpected->args[1]);
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_kind_check(expected = %d)", pos, kyexpected->node);
    }
}

static
void
gstypes_kind_check_simple(gsinterned_string file, int lineno, struct gskind *kyactual)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

    switch (kyactual->node) {
        case gskind_lifted:
            return;
        default:
            gsfatal_unimpl_at(__FILE__, __LINE__, file, lineno, "gstypes_kind_check_simple(actual = %s)", actual_name);
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

#ifdef SHOULD_DEBUG
static int debug = SHOULD_DEBUG;
#else
static int debug = 1;
#endif

void
gsfatal_unimpl_type(char *file, int lineno, struct gstype *ty, char * err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    if (debug)
        gsfatal("%s:%d: %s:%d: %s next", file, lineno, ty->file->name, ty->lineno, buf)
    ; else
        gsfatal("%s:%d: Panic: Un-implemented operation in release build: %s", ty->file->name, ty->lineno, buf)
    ;
}

void
gsfatal_bad_type(gsinterned_string file, int lineno, struct gstype *ty, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    gsfatal("%s:%d: Bad type %s:%d: %s", file->name, lineno, ty->file->name, ty->lineno, buf);
}

static
void
gsbc_typecheck_check_boxed(struct gsparsedline *p, struct gstype *type)
{
    switch (type->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)type;
            gsbc_typecheck_check_boxed(p, indir->referent);
            return;
        }
        case gstype_lift:
            return;
        case gstype_app:
            return;
        case gstype_prim:
            return;
        case gstype_fun:
            return;
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "%s:%d: gsbc_typecheck_check_boxed(node = %d)", p->pos.file->name, p->pos.lineno, type->node);
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
                gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v, "gstypes_process_data_type_signature(type = %d)", items[i].type);
        }
    }
}

static
void
gstypes_process_data_type_signature(struct gsfile_symtable *symtable, struct gsbc_item item, struct gstype **pentrytype)
{
    struct gsparsedline *pdata;

    pdata = item.v;

    if (gssymeq(pdata->directive, gssymdatadirective, ".undefined")) {
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
    } else if (gssymeq(pdata->directive, gssymdatadirective, ".closure")) {
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
    } else if (gssymeq(pdata->directive, gssymdatadirective, ".cast")) {
        return;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "gstypes_process_data_type_signature(%s)", pdata->directive->name);
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
            gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v, "gstypes_kind_check_scc(type = %d)", items[i].type);
    }
}

static void gstypes_type_check_type_fail(struct gsparsedline *, struct gstype *, struct gstype *);
static void gsbc_typecheck_check_boxed(struct gsparsedline *p, struct gstype *type);

static
void
gstypes_type_check_data_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, struct gstype **pentrytype, int n, int i)
{
    struct gsparsedline *pdata;

    pdata = items[i].v;
    if (gssymeq(pdata->directive, gssymdatadirective, ".undefined")) {
        struct gstype *type;
        struct gskind *kind;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "couldn't find type '%s'", pdata->arguments[0]->name)
        ;
        kind = gssymtable_get_type_expr_kind(symtable, pdata->arguments[0]);
        if (!kind)
            gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "couldn't find kind of '%s'", pdata->arguments[0]->name)
        ;
        gstypes_kind_check(pdata->pos, kind, gskind_lifted_kind());
    } else if (gssymeq(pdata->directive, gssymdatadirective, ".closure")) {
        struct gsbc_code_item_type *code_type;

        gsargcheck(pdata, 0, "code");
        code_type = gssymtable_get_code_type(symtable, pdata->arguments[0]);
        if (!code_type)
            gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "Cannot find type of code item %s", pdata->arguments[0]->name)
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
            gstypes_type_check_type_fail(pdata, code_type->result_type, declared_type);
            gsbc_typecheck_check_boxed(pdata, declared_type);
        } else {
            if (pdata->label)
                gssymtable_set_data_type(symtable, pdata->label, code_type->result_type)
            ; else if (pentrytype)
                *pentrytype = code_type->result_type
            ;
            gsbc_typecheck_check_boxed(pdata, code_type->result_type);
        }
    } else if (gssymeq(pdata->directive, gssymdatadirective, ".cast")) {
        struct gstype *src_type;
        struct gsbc_coercion_type *coercion_type;

        coercion_type = gssymtable_get_coercion_type(symtable, pdata->arguments[0]);
        if (!coercion_type)
            gsfatal_bad_input(pdata, "Couldn't find type of coercion %s", pdata->arguments[0]->name);

        src_type = gssymtable_get_data_type(symtable, pdata->arguments[1]);
        if (!src_type)
            gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "Couldn't find type of %s", pdata->arguments[1]->name);

        gstypes_type_check_type_fail(pdata, src_type, coercion_type->source);

        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, coercion_type->dest)
        ; else if (pentrytype)
            *pentrytype = coercion_type->dest
        ;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "gstypes_type_check_data_item(%s)", pdata->directive->name);
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
        gsfatal_unimpl_input(__FILE__, __LINE__, pcode, "gstypes_type_check_code_item(%s)", pcode->directive->name);
    }
}

static struct gs_block_class gsbc_code_type_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Code item types",
};
static void *gsbc_code_type_nursury;

static
struct gsbc_code_item_type *
gsbc_typecheck_code_expr(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    enum {
        rttygvar,
        rtgvar,
        rtarg,
    } regtype;
    int i;
    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    struct gstype *regtypes[MAX_NUM_REGISTERS];
    struct gstype *tyregs[MAX_NUM_REGISTERS];
    struct gskind *tyregkinds[MAX_NUM_REGISTERS];
    int nargs;
    struct gstype *argtypes[MAX_NUM_REGISTERS];
    struct gsparsedline *arglines[MAX_NUM_REGISTERS];
    struct gstype *calculated_type;
    struct gskind *kind;
    struct gsbc_code_item_type *res;

    nregs = 0;
    nargs = 0;
    regtype = rttygvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (regtype > rttygvar)
                gsfatal_bad_input(p, "Too late to add global type variables")
            ;
            regtype = rttygvar;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            regs[nregs] = p->label;
            tyregs[nregs] = gssymtable_get_type(symtable, p->label);
            if (!tyregs[nregs])
                gsfatal_bad_input(p, "Couldn't find type global %s", p->label->name)
            ;
            tyregkinds[nregs] = gssymtable_get_type_expr_kind(symtable, p->label);
            if (!tyregkinds[nregs])
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "couldn't find kind of '%s'", p->label->name)
            ;
            nregs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
            if (regtype > rtgvar)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            regtype = rtgvar;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            regs[nregs] = p->label;
            regtypes[nregs] = gssymtable_get_data_type(symtable, p->label);
            if (!regtypes[nregs])
                gsfatal_bad_input(p, "Couldn't find type for global %s", p->label->name)
            ;
            nregs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".arg")) {
            int reg;
            struct gstype *argtype;

            if (regtype > rtarg)
                gsfatal_bad_input(p, "Too late to add arguments")
            ;
            regtype = rtarg;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers")
            ;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "var");
            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            argtype = tyregs[reg];
            if (!argtype)
                gsfatal_bad_input(p, "Register %s is not a type", p->arguments[0]);
            for (i = 1; i < p->numarguments; i++) {
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Type arguments");
            }
            regtypes[nregs] = argtype;
            nregs++;

            argtypes[nargs] = argtype;
            arglines[nargs] = p;
            nargs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            int reg = 0;

            gsargcheck(p, 0, "var");
            for (i = 0; i < nregs; i++) {
                if (regs[i] == p->arguments[0]) {
                    reg = i;
                    goto have_reg_for_enter;
                }
            }
            gsfatal_bad_input(p, "No such register %s", p->arguments[0]->name);
        have_reg_for_enter:
            calculated_type = regtypes[reg];
            goto have_type;
        } else if (gssymeq(p->directive, gssymcodeop, ".undef")) {
            int reg;

            gsargcheck(p, 0, "type");
            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            calculated_type = tyregs[reg];
            kind = tyregkinds[reg];
            for (i = 1; i < p->numarguments; i++) {
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "arguments to .undef type");
            }
            gstypes_kind_check(p->pos, kind, gskind_lifted_kind());
            goto have_type;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_code_expr(%s)", p->directive->name);
        }
    }

have_type:

    while (nargs--) {
        struct gstype *ty;

        ty = argtypes[nargs];
        p = arglines[nargs];

        calculated_type = gstypes_compile_fun(p->pos, ty, calculated_type);
    }

    res = gs_sys_seg_suballoc(&gsbc_code_type_descr, &gsbc_code_type_nursury, sizeof(*res), sizeof(int));
    res->numftyvs = 0;
    res->numfvs = 0;
    res->result_type = calculated_type;

    return res;
}

static struct gstype *gsbc_typecheck_check_api_statement_type(struct gspos, struct gstype *, gsinterned_string, gsinterned_string, int *);
static int gsbc_typecheck_free_type_variables(struct gsparsedline *, struct gsbc_code_item_type *);
static int gsbc_typecheck_free_variables(struct gsparsedline *, struct gsbc_code_item_type *, int);

static
struct gsbc_code_item_type *
gsbc_typecheck_api_expr(struct gspos pos, struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, gsinterned_string primsetname, gsinterned_string prim)
{
    enum {
        rttygvar,
        rtgvar,
        rtcode,
        rtarg,
        rtgen,
    } regtype;
    int nregs, ncodes, nargs, nbinds;
    gsinterned_string regs[MAX_NUM_REGISTERS], coderegs[MAX_NUM_REGISTERS];
    struct gstype *tyregs[MAX_NUM_REGISTERS];
    struct gskind *tyregkinds[MAX_NUM_REGISTERS];
    struct gsbc_code_item_type *codetypes[MAX_NUM_REGISTERS];
    struct gstype *regtypes[MAX_NUM_REGISTERS];
    int first_rhs_lifted;
    struct gstype *argtypes[MAX_NUM_REGISTERS];
    struct gsparsedline *arglines[MAX_NUM_REGISTERS];
    struct gstype *calculated_type;
    struct gsbc_code_item_type *res;

    nregs = ncodes = nargs = nbinds = 0;
    regtype = rttygvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (regtype > rtcode)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            regtype = rtcode;
            if (ncodes >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            coderegs[ncodes] = p->label;
            codetypes[ncodes] = gssymtable_get_code_type(symtable, p->label);
            if (!codetypes[ncodes])
                gsfatal_bad_input(p, "Can't find type of sub-expression %s", p->label->name)
            ;
            ncodes++;
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;
            int nftyvs;

            if (regtype > rtgen)
                gsfatal_bad_input(p, "Too late to add generators")
            ;
            regtype = rtgen;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, coderegs, ncodes, p->arguments[0]);
            cty = codetypes[creg];
            calculated_type = cty->result_type;

            nftyvs = gsbc_typecheck_free_type_variables(p, cty);
            gsbc_typecheck_free_variables(p, cty, nftyvs);

            regs[nregs] = p->label;
            regtypes[nregs] = gsbc_typecheck_check_api_statement_type(p->pos, calculated_type, primsetname, prim, nbinds == 0 ? &first_rhs_lifted : 0);

            nregs++;
            nbinds++;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int creg = 0;
            struct gsbc_code_item_type *cty;
            int nftyvs;

            gsargcheck(p, 0, "code");
            creg = gsbc_find_register(p, coderegs, ncodes, p->arguments[0]);
            cty = codetypes[creg];
            calculated_type = cty->result_type;

            nftyvs = gsbc_typecheck_free_type_variables(p, cty);
            gsbc_typecheck_free_variables(p, cty, nftyvs);

            gsbc_typecheck_check_api_statement_type(p->pos, calculated_type, primsetname, prim, nbinds == 0 ? &first_rhs_lifted : 0);
            goto have_type;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_api_expr(%s)", p->directive->name);
        }
    }

have_type:

    calculated_type = gstypes_clear_indirections(calculated_type);

    if (first_rhs_lifted) {
        if (calculated_type->node != gstype_lift)
            calculated_type = gstypes_compile_lift(pos, calculated_type)
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Ensure API block statement has unlifted type", pos);
    }

    while (nargs--) {
        struct gstype *ty;

        ty = argtypes[nargs];
        p = arglines[nargs];

        calculated_type = gstypes_compile_fun(p->pos, ty, calculated_type);
    }

    res = gs_sys_seg_suballoc(&gsbc_code_type_descr, &gsbc_code_type_nursury, sizeof(*res), sizeof(int));
    res->numftyvs = 0;
    res->numfvs = 0;
    res->result_type = calculated_type;

    return res;
}

static
int
gsbc_typecheck_free_type_variables(struct gsparsedline *p, struct gsbc_code_item_type *cty)
{
    int i;
    int nftyvs;

    for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_api_expr(.body type free variables)");
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
gsbc_typecheck_free_variables(struct gsparsedline *p, struct gsbc_code_item_type *cty, int nftyvs)
{
    int i;
    int nfvs;

    i = 1 + nftyvs;
    if (i < p->numarguments) i++;
    nfvs = p->numarguments - i;
    if (nfvs < cty->numfvs)
        gsfatal_bad_input(p, "Not enough free variables for %s; need %d but have %d", p->arguments[0]->name, cty->numfvs, nfvs)
    ;
    if (nfvs > cty->numfvs)
        gsfatal_bad_input(p, "Too many free variables for %s; only need %d but have %d", p->arguments[0]->name, cty->numfvs, nfvs)
    ;
    for (; i < p->numarguments; i++) {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_api_expr(.body free variables)");
    }
    return nfvs;
}

static
struct gstype *
gsbc_typecheck_check_api_statement_type(struct gspos pos, struct gstype *ty, gsinterned_string primsetname, gsinterned_string primname, int *plifted)
{
    char buf[0x100];
    struct gstype *res;

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(buf, buf + sizeof(buf), ty) >= buf + sizeof(buf))
        gsfatal("%s:%d: %P: buffer overflow printing type %s:%d", __FILE__, __LINE__, pos, ty->file->name, ty->lineno)
    ;

    if (ty->node == gstype_lift) {
        struct gstype_lift *lift;

        lift = (struct gstype_lift *)ty;
        ty = lift->arg;

        if (plifted)
            *plifted = 1
        ;
    }

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

    if (ty->node != gstype_prim) {
        gsfatal("%P: I don't think %s is an application of a primitive", pos, buf);
    } else {
        struct gstype_prim *prim;

        prim = (struct gstype_prim *)ty;

        if (prim->primtypegroup != gsprim_type_api)
            gsfatal("%P: I don't think %s is an API primitive type", pos, buf)
        ;
        if (prim->primsetname != primsetname)
            gsfatal("%P: I don't think %s is a type in the %s primset", pos, buf, primsetname->name)
        ;
        if (prim->name != primname)
            gsfatal("%P: I don't think %s is the primtype %s %s", pos, buf, primsetname->name, primname->name)
        ;
    }

    return res;
}

static
void
gstypes_type_check_type_fail(struct gsparsedline *p, struct gstype *pactual, struct gstype *pexpected)
{
    char err[0x100];

    if (gstypes_type_check(p->pos, pactual, pexpected, err, err + sizeof(err)) < 0)
        gsfatal("%s", err)
    ;
}

int
gstypes_type_check(struct gspos pos, struct gstype *pactual, struct gstype *pexpected, char *err, char *eerr)
{
    char actual_buf[0x100];
    char expected_buf[0x100];

    pactual = gstypes_clear_indirections(pactual);
    pexpected = gstypes_clear_indirections(pexpected);
    
    if (gstypes_eprint_type(actual_buf, actual_buf + sizeof(actual_buf), pactual) >= actual_buf + sizeof(actual_buf)) {
        seprint(err, eerr, "%s:%d: %s:%d: buffer overflow printing actual type %s:%d", __FILE__, __LINE__, pos.file->name, pos.lineno, pactual->file->name, pactual->lineno);
        return -1;
    }
    if (gstypes_eprint_type(expected_buf, expected_buf + sizeof(expected_buf), pexpected) >= actual_buf + sizeof(actual_buf)) {
        seprint(err, eerr, "%s:%d: %s:%d: buffer overflow printing actual type %s:%d", __FILE__, __LINE__, pos.file->name, pos.lineno, pactual->file->name, pactual->lineno);
        return -1;
    }

    if (pactual->node != pexpected->node) {
        seprint(err, eerr, "%s:%d: I don't think %s (%s:%d) is %s (%s:%d)",
            pos.file->name, pos.lineno,
            actual_buf, pactual->file->name, pactual->lineno, 
            expected_buf, pexpected->file->name, pexpected->lineno
        );
        return -1;
    }

    switch (pexpected->node) {
        case gstype_prim: {
            struct gstype_prim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_prim *)pactual;
            pexpected_prim = (struct gstype_prim *)pexpected;

            if (pactual_prim->primset != pexpected_prim->primset) {
                seprint(err, eerr, "%s:%d: I don't think primset %s is the same as primset %s", pos.file->name, pos.lineno, pactual_prim->primset->name, pexpected_prim->primset->name);
                return -1;
            }
            if (pactual_prim->name != pexpected_prim->name) {
                seprint(err, eerr, "%s:%d: I don't think prim %s is the same as prim %s", pos.file->name, pos.lineno, pactual_prim->name->name, pexpected_prim->name->name);
                return -1;
            }
            return 0;
        }
        case gstype_abstract: {
            struct gstype_abstract *pactual_abstract, *pexpected_abstract;

            pactual_abstract = (struct gstype_abstract *)pactual;
            pexpected_abstract = (struct gstype_abstract *)pexpected;

            if (pactual_abstract->name != pexpected_abstract->name) {
                seprint(err, eerr, "%s:%d: I don't think abstype %s is the same as abstype %s", pos.file->name, pos.lineno, pactual_abstract->name->name, pexpected_abstract->name->name);
                return -1;
            }
            return 0;
        }
        case gstype_lift: {
            struct gstype_lift *pactual_lift, *pexpected_lift;

            pactual_lift = (struct gstype_lift *)pactual;
            pexpected_lift = (struct gstype_lift *)pexpected;

            return gstypes_type_check(pos, pactual_lift->arg, pexpected_lift->arg, err, eerr);
        }
        case gstype_app: {
            struct gstype_app *pactual_app, *pexpected_app;

            pactual_app = (struct gstype_app *)pactual;
            pexpected_app = (struct gstype_app *)pexpected;

            if (gstypes_type_check(pos, pactual_app->fun, pexpected_app->fun, err, eerr) < 0)
                return -1;
            if (gstypes_type_check(pos, pactual_app->arg, pexpected_app->arg, err, eerr) < 0)
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
                seprint(err, eerr, "%s:%d: %s:%d: %s:%d: gstypes_check_type(check constrs)", __FILE__, __LINE__, pos.file->name, pos.lineno, pexpected->file->name, pexpected->lineno);
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
                seprint(err, eerr, "%s:%d: %s:%d: %s:%d: gstypes_check_type(check fields)", __FILE__, __LINE__, pos.file->name, pos.lineno, pexpected->file->name, pexpected->lineno);
                return -1;
            }
            return 0;
        }
        default:
            seprint(err, eerr, "gstypes_check_type(node = %d) %s:%d: %s:%d:", __FILE__, __LINE__, pexpected->node, pos.file->name, pos.lineno, pexpected->file->name, pexpected->lineno);
            return -1;
    }
}

struct gstype *
gstypes_clear_indirections(struct gstype *pty)
{
    if (pty->node == gstype_indirection) {
        struct gstype_indirection *indir;

        indir = (struct gstype_indirection *)pty;

        return indir->referent;
    } else {
        return pty;
    }
}

char *
gstypes_eprint_type(char *res, char *eob, struct gstype *pty)
{
    int i;

    switch (pty->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)pty;

            res = gstypes_eprint_type(res, eob, indir->referent);
            return res;
        }
        case gstype_prim: {
            struct gstype_prim *prim;

            prim = (struct gstype_prim *)pty;

            switch (prim->primtypegroup) {
                case gsprim_type_api:
                    res = seprint(res, eob, "\"apiprim ");
                    break;
                default:
                    res = seprint(res, eob, "%s:%d: %d primitives next ", __FILE__, __LINE__, prim->primtypegroup);
                    break;
            }
            res = seprint(res, eob, "%s ", prim->primsetname->name);
            res = seprint(res, eob, "%s", prim->name->name);
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
        gsfatal_unimpl_input(__FILE__, __LINE__, pcoercion, "gstypes_type_check_coercion_item(%s)", pcoercion->directive->name);
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
    int i, j;
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
            kind = gskind_compile(p, p->arguments[0]);
            regtypes[nregs] = gstypes_compile_type_var(p->pos.file, p->pos.lineno, p->label, kind);
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

            reg = -1;
            for (i = 0; i < nregs; i++) {
                if (regs[i] == p->arguments[0]) {
                    reg = i;
                    goto have_register_for_defn;
                }
            }
            gsfatal_bad_input(p, "Can't find register for abstract type %s", p->arguments[0]->name);
        have_register_for_defn:
            if (reg >= nglobals)
                gsfatal_bad_input(p, "Register %s isn't a global; can't really cast to/from definition of an abstract type unless we know what that definition is", p->arguments[0]->name)
            ;
            global = reg;
            if (!regtypes[reg])
                gsfatal_bad_input(p, "Register %s doesn't seem to be a type variable", p->arguments[0]->name);
            dest = gstypes_clear_indirections(regtypes[global]);

            if (!globaldefns[global])
                gsfatal_bad_input(p, "Register %s doesn't seem to be an abstract type", p->arguments[0]->name);
            source = gstypes_clear_indirections(globaldefns[global]);

            for (i = 1; i < p->numarguments; i++) {
                for (j = 0; j < nregs; j++) {
                    if (regs[j] == p->arguments[i]) {
                        reg = j;
                        goto have_register_for_arg;
                    }
                }
                gsfatal_bad_input(p, "Can't find type register for %s", p->arguments[i]->name);
            have_register_for_arg:
                if (!regtypes[reg])
                    gsfatal_bad_input(p, "Register %s doesn't seem to be a type variable", p->arguments[i]->name)
                ;
                source = gstype_supply(p->pos.file, p->pos.lineno, source, regtypes[reg]);
                dest = gstype_apply(p->pos.file, p->pos.lineno, dest, regtypes[reg]);
            }

            if (source->node == gstype_lambda)
                gsfatal_bad_input(p, "Not enough arguments to definition of %s", p->arguments[0]->name)
            ;

            goto have_type;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_coercion_expr(%s)", p->directive->name);
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
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_coercion_expr(cont %s)", p->directive->name);
        }
    }

    while (nargs--) {
        gsinterned_string var;
        struct gskind *kind;
        gsinterned_string file;
        int lineno;

        var = regs[nglobals + nargs];
        kind = argkinds[nargs];
        file = arglines[nargs]->pos.file;
        lineno = arglines[nargs]->pos.lineno;

        source = gstypes_compile_lambda(file, lineno, var, kind, source);
        dest = gstypes_compile_lambda(file, lineno, var, kind, dest);
    }

    res = gs_sys_seg_suballoc(&gsbc_coercion_type_descr, &gsbc_coercion_type_nursury, sizeof(*res), sizeof(void*));
    res->source = source;
    res->dest = dest;

    return res;
}
