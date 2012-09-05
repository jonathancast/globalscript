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
            case gssymcodelable:
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
        case gssymcodelable:
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
        case gstype_prim:
            return;
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "%s:%d: gsbc_typecheck_check_boxed(node = %d)", p->file->name, p->lineno, type->node);
    }
}

static void gstypes_process_data_type_signature(struct gsfile_symtable *, struct gsbc_item);

void
gstypes_process_type_signatures(struct gsfile_symtable *symtable, struct gsbc_item *items, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymdatalable:
                gstypes_process_data_type_signature(symtable, items[i]);
                break;
            case gssymcodelable:
            case gssymtypelable:
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v.ptype, "gstypes_process_data_type_signature(type = %d)", items[i].type);
        }
    }
}

static
void
gstypes_process_data_type_signature(struct gsfile_symtable *symtable, struct gsbc_item item)
{
    struct gsparsedline *pdata;

    pdata = item.v.pdata;

    if (gssymeq(pdata->directive, gssymdatadirective, ".undefined")) {
        struct gstype *type;

        gsargcheck(pdata, 0, "type");
        type = gssymtable_get_type(symtable, pdata->arguments[0]);
        if (!type)
            gsfatal_bad_input(pdata, "Couldn't find type %s", pdata->arguments[0])
        ;
        if (pdata->label)
            gssymtable_set_data_type(symtable, pdata->label, type)
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
        ;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, item.v.pdata, "gstypes_process_data_type_signature(%s)", pdata->directive->name);
    }
}

static void gstypes_type_check_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);

void
gstypes_type_check_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n)
{
    int i;

    for (i = 0; i < n; i++)
        gstypes_type_check_item(symtable, items, types, kinds, n, i)
    ;
}

static void gstypes_type_check_data_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);
static void gstypes_type_check_code_item(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int, int);

static
void
gstypes_type_check_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    switch (items[i].type) {
        case gssymtypelable:
            return;
        case gssymdatalable:
            gstypes_type_check_data_item(symtable, items, types, kinds, n, i);
            return;
        case gssymcodelable:
            gstypes_type_check_code_item(symtable, items, types, kinds, n, i);
            return;
        default:
            gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v.ptype, "gstypes_kind_check_scc(type = %d)", items[i].type);
    }
}

static void gstypes_check_type(struct gsparsedline *, struct gstype *, struct gstype *);
static void gsbc_typecheck_check_boxed(struct gsparsedline *p, struct gstype *type);

static
void
gstypes_type_check_data_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    struct gsparsedline *pdata;

    pdata = items[i].v.pdata;
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
        gstypes_kind_check(pdata->file, pdata->lineno, kind, gskind_lifted_kind());
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
            gstypes_check_type(pdata, code_type->result_type, declared_type);
            gsbc_typecheck_check_boxed(pdata, declared_type);
        } else {
            gssymtable_set_type(symtable, pdata->label, code_type->result_type);
            gsbc_typecheck_check_boxed(pdata, code_type->result_type);
        }
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, pdata, "gstypes_type_check_data_item(%s)", pdata->directive->name);
    }
}

static struct gsbc_code_item_type *gsbc_typecheck_code_expr(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *);

static
void
gstypes_type_check_code_item(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gskind **kinds, int n, int i)
{
    struct gsparsedline *pcode;
    struct gsparsedfile_segment *pseg;

    pseg = items[i].pseg;
    pcode = items[i].v.pcode;

    if (gssymeq(pcode->directive, gssymcodedirective, ".expr")) {
        struct gsbc_code_item_type *type;

        type = gsbc_typecheck_code_expr(symtable, &pseg, gsinput_next_line(&pseg, pcode));
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
        rtgvar,
    } regtype;
    int i;
    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    struct gstype *regtypes[MAX_NUM_REGISTERS];
    struct gstype *calculated_type;
    struct gsbc_code_item_type *res;

    nregs = 0;
    regtype = rtgvar;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
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
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            int reg = 0;

            gsargcheck(p, 0, "var");
            for (i = 0; i < nregs; i++) {
                if (regs[i] == p->arguments[0]) {
                    reg = i;
                    goto have_reg;
                }
            }
            gsfatal_bad_input(p, "No such register %s", p->arguments[0]->name);
        have_reg:
            calculated_type = regtypes[reg];
            goto have_type;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_typecheck_code_expr(%s)", p->directive->name);
        }
    }

have_type:

    res = gs_sys_seg_suballoc(&gsbc_code_type_descr, &gsbc_code_type_nursury, sizeof(*res), sizeof(int));
    res->numftyvs = 0;
    res->numfvs = 0;
    res->result_type = calculated_type;

    return res;
}

static struct gstype *gstypes_clear_indirections(struct gstype *);

static char *gstypes_eprint_type(char *, char *, struct gstype *);

static
void
gstypes_check_type(struct gsparsedline *p, struct gstype *pactual, struct gstype *pexpected)
{
    char actual_buf[0x100];
    char expected_buf[0x100];

    pactual = gstypes_clear_indirections(pactual);
    pexpected = gstypes_clear_indirections(pexpected);
    
    if (gstypes_eprint_type(actual_buf, actual_buf + sizeof(actual_buf), pactual) >= actual_buf + sizeof(actual_buf))
        gsfatal("%s:%d: %s:%d: buffer overflow printing actual type %s:%d", __FILE__, __LINE__, p->file->name, p->lineno, pactual->file->name, pactual->lineno)
    ;
    if (gstypes_eprint_type(expected_buf, expected_buf + sizeof(expected_buf), pexpected) >= actual_buf + sizeof(actual_buf))
        gsfatal("%s:%d: %s:%d: buffer overflow printing actual type %s:%d", __FILE__, __LINE__, p->file->name, p->lineno, pactual->file->name, pactual->lineno)
    ;

    if (pactual->node != pexpected->node)
        gsfatal_bad_input(p, "I don't think %s (%s:%d) is %s (%s:%d)",
            actual_buf, pactual->file->name, pactual->lineno, 
            expected_buf, pexpected->file->name, pexpected->lineno
        )
    ;

    switch (pexpected->node) {
        case gstype_prim: {
            struct gstype_prim *pactual_prim, *pexpected_prim;

            pactual_prim = (struct gstype_prim *)pactual;
            pexpected_prim = (struct gstype_prim *)pexpected;

            if (pactual_prim->primset != pexpected_prim->primset)
                gsfatal_bad_input(p, "I don't think primset %s is the same as primset %s", pactual_prim->primset->name, pexpected_prim->primset->name)
            ;
            if (pactual_prim->name != pexpected_prim->name)
                gsfatal_bad_input(p, "I don't think prim %s is the same as prim %s", pactual_prim->name->name, pexpected_prim->name->name)
            ;
            return;
        }
        case gstype_lift: {
            struct gstype_lift *pactual_lift, *pexpected_lift;

            pactual_lift = (struct gstype_lift *)pactual;
            pexpected_lift = (struct gstype_lift *)pexpected;

            gstypes_check_type(p, pactual_lift->arg, pexpected_lift->arg);
            return;
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, pexpected, "%s:%d: gstypes_check_type(node = %d)", p->file->name, p->lineno, pexpected->node);
    }
}

static
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

static
char *
gstypes_eprint_type(char *res, char *eob, struct gstype *pty)
{
    switch (pty->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)pty;

            res = seprint(res, eob, "\"indir ");
            res = gstypes_eprint_type(res, eob, indir->referent);
            return res;
        }
        default:
            res = seprint(res, eob, "unknown type node %d", pty->node);
            return res;
    }
}
