/* §source.file{String-Code Type Checker} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
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

                    ptype = item.v.ptype;
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

            if (kinds[i])
                gstypes_kind_check(type->file, type->lineno, calculated_kind, kinds[i]);
            else {
                kinds[i] = calculated_kind;
                gssymtable_set_type_expr_kind(symtable, items[i].v.ptype->label, calculated_kind);
            }
            return;

            switch (type->node) {
                case gstype_abstract: {
                    gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: kind check abstract type next", type->file->name, type->lineno);
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

static void gstypes_kind_check_simple(gsinterned_string, int, struct gskind *);

struct gskind *
gstypes_calculate_kind(struct gstype *type)
{

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

            lift = (struct gstype_lift *)type;

            kyarg = gstypes_calculate_kind(lift->arg);
            gstypes_kind_check(type->file, type->lineno, kyarg, gskind_unlifted_kind());

            return gskind_lifted_kind();
        }
        case gstype_app: {
            struct gstype_app *app;
            struct gskind *funkind, *argkind;

            app = (struct gstype_app *)type;
            funkind = gstypes_calculate_kind(app->fun);
            argkind = gstypes_calculate_kind(app->arg);

            switch (funkind->node) {
                case gskind_exponential: {
                    gstypes_kind_check(type->file, type->lineno, argkind, funkind->args[1]);
                    return funkind->args[0];
                }
                default:
                    gsfatal_unimpl_type(__FILE__, __LINE__, type, "'function' kind (node = %d)", funkind->node);
            }
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "gstypes_calculate_kind(node = %d)", type->node);
    }
    return 0;
}

#define MAX_NUM_REGISTERS 0x100

static void seprint_kind_name(char *, char *, struct gskind *);

void
gstypes_kind_check(gsinterned_string file, int lineno, struct gskind *kyactual, struct gskind *kyexpected)
{
    char actual_name[0x100];

    seprint_kind_name(actual_name, actual_name + sizeof(actual_name), kyactual);

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
void
seprint_kind_name(char *buf, char *ebuf, struct gskind *ky)
{
    switch (ky->node) {
        case gskind_unknown:
            seprint(buf, ebuf, "?");
            break;
        case gskind_lifted:
            seprint(buf, ebuf, "*");
            break;
        default:
            seprint(buf, ebuf, "%s:%d: Unknown kind type %d", __FILE__, __LINE__, ky->node);
            break;
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
