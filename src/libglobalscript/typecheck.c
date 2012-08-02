/* §source.file{String-Code Type Checker} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

static struct gs_block_class gstype_item_kind_descr = {
    /* evaluator = */ gsnoeval,
    /* descrption = */ "GSBC Type Item Kind",
};
static void *gstype_item_kind_nursury;

void
gstypes_process_type_declarations(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype_item_kind **kinds, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedline *ptype;

                    ptype = item.v.ptype;
                    if (gssymeq(ptype->directive, gssymtypedirective, ".tyabstract")) {
                        struct gskind *ky;

                        gsargcheck(ptype, 0, "Kind");

                        ky = gskind_compile(ptype, ptype->arguments[0]);
                        kinds[i] = gs_sys_seg_suballoc(&gstype_item_kind_descr, &gstype_item_kind_nursury, sizeof(*kinds[i]), sizeof(struct gskind *));
                        kinds[i]->numfvs = 0;
                        kinds[i]->fvkinds = 0;
                        kinds[i]->kind = ky;

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyexpr")) {
                        struct gskind *ky;

                        if (ptype->numarguments > 0) {
                            ky = gskind_compile(ptype, ptype->arguments[0]);
                            kinds[i] = gs_sys_seg_suballoc(&gstype_item_kind_descr, &gstype_item_kind_nursury, sizeof(*kinds[i]), sizeof(struct gskind *));
                            kinds[i]->numfvs = 0;
                            kinds[i]->fvkinds = 0;
                            kinds[i]->kind = ky;

                            gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                        } else {
                            kinds[i] = 0;
                        }
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tydefinedprim")) {
                        struct gskind *ky;

                        gsargcheck(ptype, 2, "Kind");

                        ky = gskind_compile(ptype, ptype->arguments[2]);
                        kinds[i] = gs_sys_seg_suballoc(&gstype_item_kind_descr, &gstype_item_kind_nursury, sizeof(*kinds[i]), sizeof(struct gskind *));
                        kinds[i]->numfvs = 0;
                        kinds[i]->fvkinds = 0;
                        kinds[i]->kind = ky;

                        gssymtable_set_type_expr_kind(symtable, ptype->label, kinds[i]);
                    } else {
                        gsfatal("%s:%d: %s:%d: gstypes_process_type_declarations(%s) next", __FILE__, __LINE__, ptype->file->name, ptype->lineno, ptype->directive->name);
                    }
                }
                break;
            case gssymdatalable:
                break;
            default:
                gsfatal("%s:%d: %s:%d: gstypes_process_type_declarations(type = %d) next", __FILE__, __LINE__, item.v.ptype->file->name, item.v.ptype->lineno, item.type);
        }
    }
}

static void gstypes_kind_check_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gstype_item_kind **, int, int);

void
gstypes_kind_check_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gstype_item_kind **kinds, int n)
{
    int i;

    for (i = 0; i < n; i++)
        gstypes_kind_check_item(symtable, items, types, kinds, n, i)
    ;
}

static struct gstype_item_kind *gstypes_kind_type_expr_summary(struct gsfile_symtable *, struct gstype *, struct gstype_expr_summary *);

static void gstypes_type_item_kind_check(gsinterned_string, int, struct gstype_item_kind *, struct gstype_item_kind *);
static void gstypes_kind_check(gsinterned_string, int, struct gskind *, struct gskind *);

static
void
gstypes_kind_check_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gstype_item_kind **kinds, int n, int i)
{
    switch (items[i].type) {
        case gssymtypelable: {
            struct gstype *type;

            type = types[i];

            switch (type->node) {
                case gstype_abstract: {
                    struct gstype_item_kind *calculated_kind;

                    calculated_kind = gstypes_kind_type_expr_summary(symtable, type, type->a.expr);

                    gstypes_type_item_kind_check(type->file, type->lineno, calculated_kind, kinds[i]);
                    return;
                }
                case gstype_expr: {
                    struct gstype_item_kind *calculated_kind;

                    calculated_kind = gstypes_kind_type_expr_summary(symtable, type, type->a.expr);

                    if (kinds[i])
                        gstypes_type_item_kind_check(type->file, type->lineno, calculated_kind, kinds[i]);
                    else {
                        kinds[i] = calculated_kind;
                        gssymtable_set_type_expr_kind(symtable, items[i].v.ptype->label, calculated_kind);
                    }
                    return;
                }
                case gstype_prim: {
                    struct gsregistered_primtype *primtype;
                    if (!type->a.prim.primset)
                        return;
                    if (!(primtype = gsprims_lookup_type(type->a.prim.primset, type->a.prim.name->name)))
                        gsfatal("%s:%d: Primset %s lacks a type member %s",
                            type->file->name,
                            type->lineno,
                            type->a.prim.primset->name,
                            type->a.prim.name->name
                        )
                    ;
                    if (!primtype->kind)
                        gsfatal_unimpl(__FILE__, __LINE__, "Panic! Primitype type %s (%s:%d) lacks a declared kind", primtype->name, primtype->file, primtype->line);
                    gstypes_kind_check(type->file, type->lineno, kinds[i]->kind, gstypes_compile_prim_kind(primtype->kind));
                    return;
                }
                default:
                    gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v.ptype, "gstypes_kind_check_scc(node = %d)", type->node);
            }
            return;
        }
        case gssymdatalable:
            return;
        default:
            gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v.ptype, "gstypes_kind_check_scc(type = %d)", items[i].type);
    }
}

static void gstypes_kind_type_expr_registers(struct gsfile_symtable *, struct gstype *, struct gstype_expr_summary *, struct gskind **);
static struct gskind *gstypes_kind_type_expr(struct gsfile_symtable *, struct gstype_expr_summary *, struct gskind **, struct gstype_expr *);

#define MAX_NUM_REGISTERS 0x100

static
struct gstype_item_kind *
gstypes_kind_type_expr_summary(struct gsfile_symtable *symtable, struct gstype *e, struct gstype_expr_summary *esummary)
{
    struct gstype_item_kind *res;
    struct gskind *kind, *kinds[MAX_NUM_REGISTERS];
    int i;

    res = gs_sys_seg_suballoc(
        &gstype_item_kind_descr,
        &gstype_item_kind_nursury,
        sizeof(*res)
            + esummary->numfvs * sizeof(*res->fvkinds)
        ,
        sizeof(struct gskind *)
    );

    res->numfvs = esummary->numfvs;
    res->fvkinds = (void*)((uchar*)res + sizeof(*res));
    for (i = 0; i < esummary->numfvs; i++) {
        res->fvkinds[i] = esummary->fvkinds[i];
    }

    gstypes_kind_type_expr_registers(symtable, e, esummary, kinds);

    kind = gstypes_kind_type_expr(symtable, esummary, kinds, esummary->code);

    i = esummary->numargs;
    while (i) {
        kind = gskind_exponential_kind(kind, esummary->argkinds[--i]);
    }

    res->kind = kind;

    return res;
}

static struct gskind *gstypes_kind_type_let(struct gsfile_symtable *, struct gstype *, struct gstype_expr_summary *, struct gskind **, int, struct gstype_expr_let *);

static
void
gstypes_kind_type_expr_registers(struct gsfile_symtable *symtable, struct gstype *e, struct gstype_expr_summary *summary, struct gskind **kinds)
{
    int i, nregs;

    for (i = 0, nregs = 0; i < summary->numglobals; i++, nregs++) {
        struct gstype_item_kind *ky;

        ky = gssymtable_get_type_expr_kind(symtable, summary->global_vars[i]);
        if (!ky)
            gsfatal("%s:%d: %s:%d: Panic! Couldn't find kind of global %s", __FILE__, __LINE__,
                summary->code->file->name,
                summary->code->lineno,
                summary->global_vars[i]->name
            )
        ;

        kinds[nregs] = ky->kind;
    }

    for (i = 0; i < summary->numfvs; i++, nregs++) {
        kinds[nregs] = summary->fvkinds[i];
    }

    for (i = 0; i < summary->numargs; i++, nregs++) {
        kinds[nregs] = summary->argkinds[i];
    }

    for (i = 0; i < summary->numforalls; i++, nregs++) {
        kinds[nregs] = summary->forallkinds[i];
    }

    for (i = 0; i < summary->numlets; i++, nregs++) {
        kinds[nregs] = gstypes_kind_type_let(symtable, e, summary, kinds, nregs, &summary->lets[i]);
    }

    if (nregs < summary->numregs)
        gsfatal_unimpl_type(__FILE__, __LINE__, e, "Registers after lets")
    ;
}

static
struct gskind *
gstypes_kind_type_let(struct gsfile_symtable *symtable, struct gstype *e, struct gstype_expr_summary *summary, struct gskind **regkinds, int nregs, struct gstype_expr_let *plet)
{
    struct gstype_item_kind *ky;
    gsinterned_string code_label;
    int i;

    code_label = summary->code_labels[plet->body];
    ky = gssymtable_get_type_expr_kind(symtable, code_label);
    if (!ky)
        gsfatal("%s:%d: %s:%d: Panic! Couldn't find kind of global %s", __FILE__, __LINE__,
            e->file->name,
            e->lineno,
            summary->code_labels[plet->body]->name
        )
    ;

    if (ky->numfvs < plet->numfvs) {
        gsfatal("%s:%d: Too many free variables; supplied %s with %d free variables but its kind only has %d",
            e->file->name,
            e->lineno,
            code_label->name,
            plet->numfvs,
            ky->numfvs
        );
    }
    if (ky->numfvs > plet->numfvs) {
        gsfatal("%s:%d: Not enough free variables; supplied %s with %d free variables but its kind needs %d",
            e->file->name,
            e->lineno,
            code_label->name,
            plet->numfvs,
            ky->numfvs
        );
    }

    for (i = 0; i < plet->numfvs; i++) {
        gstypes_kind_check(e->file, e->lineno, regkinds[plet->fvs[i]], ky->fvkinds[i]);
    }

    return ky->kind;
}

static
struct gskind *
gstypes_kind_type_expr(struct gsfile_symtable *symtable, struct gstype_expr_summary *summary, struct gskind **regkinds, struct gstype_expr *code)
{
    switch (code->node) {
        case gstype_lift: {
            struct gstype_expr_lift *lift;
            struct gskind *kyarg;

            lift = (struct gstype_expr_lift *)code;

            kyarg = gstypes_kind_type_expr(symtable, summary, regkinds, lift->arg);
            gstypes_kind_check(code->file, code->lineno, kyarg, gskind_unlifted_kind());

            return gskind_lifted_kind();
        }
        case gstype_app: {
            struct gstype_expr_app *app;
            struct gskind *kyarg, *kyfun;
            int i;

            app = (struct gstype_expr_app *)code;

            kyfun = gstypes_kind_type_expr(symtable, summary, regkinds, app->fun);

            for (i = 0; i < app->numargs; i++) {
                switch (kyfun->node) {
                    case gskind_exponential:
                        kyarg = regkinds[app->args[i]];
                        gstypes_kind_check(app->e.file, app->e.lineno, kyarg, kyfun->args[1]);
                        kyfun = kyfun->args[0];
                        break;
                    default:
                        gsfatal_unimpl(__FILE__, __LINE__, "gsbc_typecheck_kind_type_app (fun kind node = %d)", kyfun->node);
                }
            }
            return kyfun;
        }
        case gstype_ref: {
            struct gstype_expr_ref *ref;

            ref = (struct gstype_expr_ref *)code;

            return regkinds[ref->referent];
        }
        case gstype_sum: {
            struct gstype_expr_sum *sum;
            int i;

            sum = (struct gstype_expr_sum *)code;

            for (i = 0; i < sum->numconstrs; i++) {
                gsfatal_unimpl_at(__FILE__, __LINE__, sum->e.file, sum->e.lineno, "gstypes_kind_type_expr(constr)");
            }

            return gskind_unlifted_kind();
        }
        default:
            gsfatal("%s:%d: %s:%d: gsbc_typecheck_kind_type_expr(node = %d) next", __FILE__, __LINE__, code->file->name, code->lineno, code->node);
    }

    gsfatal("%s:%d: gsbc_typecheck_kind_type_expr next", __FILE__, __LINE__);

    return 0;
}

static
void
gstypes_type_item_kind_check(gsinterned_string file, int lineno, struct gstype_item_kind *kyactual, struct gstype_item_kind *kyexpected)
{
    gstypes_kind_check(file, lineno, kyactual->kind, kyexpected->kind);
}

static
void
gstypes_kind_check(gsinterned_string file, int lineno, struct gskind *kyactual, struct gskind *kyexpected)
{
    char actual_name[0x100];

    switch (kyactual->node) {
        case gskind_unknown:
            seprint(actual_name, actual_name + sizeof(actual_name), "?");
            break;
        case gskind_lifted:
            seprint(actual_name, actual_name + sizeof(actual_name), "*");
            break;
        default:
            seprint(actual_name, actual_name + sizeof(actual_name), "%s:%d: Unknown kind type %d", __FILE__, __LINE__, kyactual->node);
            break;
    }

    switch (kyexpected->node) {
        case gskind_unknown:
            if (kyactual->node != gskind_unknown && kyactual->node != gskind_unlifted && kyactual->node != gskind_lifted)
                gsfatal("%s:%d: Incorrect kind: Expected '?'; got '%s'", file->name, lineno, actual_name);
            return;
        case gskind_unlifted:
            if (kyactual->node != gskind_unlifted)
                gsfatal("%s:%d: Incorrect kind: Expected 'u'; got '%s'", file->name, lineno, actual_name);
            return;
        case gskind_lifted:
            if (kyactual->node != gskind_lifted)
                gsfatal("%s:%d: Incorrect kind: Expected '*'; got '%s'", file->name, lineno, actual_name);
            return;
        case gskind_exponential:
            if (kyactual->node != gskind_exponential)
                gsfatal("%s:%d: Incorrect kind: Expected '^'; got '%s'", file->name, lineno, actual_name);
            gstypes_kind_check(file, lineno, kyactual->args[0], kyexpected->args[0]);
            gstypes_kind_check(file, lineno, kyactual->args[1], kyexpected->args[1]);
            return;
        default:
            gsfatal_unimpl_at(__FILE__, __LINE__, file, lineno, "gstypes_kind_check(expected = %d)", kyexpected->node);
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

#ifdef typechecking
static
struct gsbc_code_item_type *
gsbc_typecheck_code_expr(struct gsfile_symtable *symtable, struct gsparsedline *p)
{
    enum {
        rtgvar,
    } regtype;
    int nregs;
/*    struct gsbc_code_item_type *regtypes[MAX_REGISTERS]; */

    /*
        Grar

        Have to go forward to set up lexical environment then go backward to get type.
        
        Here's what let's do:
        §begin{itemize}
            §item Go forward
            §item Keep a stack of the continuation ops as we go
            §item Build the type when we reach the end
            §item Then do another loop backward over the stack mutating the type as we go
            §item Also do type-checking in this second loop
        §end{itemize}
    */
    regtype = rtgvar;
    for (; ; p = gsinput_next_line(p)) {
        if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
            struct gsbc_data_item_type *varty;

            if (regtype != rtgvar)
                gsfatal("%s:%d: %s:%d: Illegal .gvar; next register should be %s",
                    __FILE__,
                    __LINE__,
                    p->file->name,
                    p->lineno,
                    "unknown"
                )
            ;
            if (nregs >= MAX_REGISTERS)
                gsfatal("%s:%d: %s:%d: Too many registers; max 0x%x", __FILE__, __LINE__, p->file->name, p->lineno, MAX_REGISTERS);
            if (varty = gssymtable_get_data_type(symtable, p->label)) {
            } else {
                gsfatal("%s:%d: %s:%d: Cannot find type of %s", __FILE__, __LINE__, p->file->name, p->lineno, p->label->name);
            }
/*            regtypes[nregs++] = varty; */
            gsfatal("%s:%d: %s:%d: Type-check .gvar next", __FILE__, __LINE__, p->file->name, p->lineno);
        } else {
            gsfatal("%s:%d: %s:%d: Cannot type-check op %s yet", __FILE__, __LINE__, p->file->name, p->lineno, p->directive->name);
        }
    }

    return 0;
}
#endif
