/* §source.file{Compiling Types Into C Data Structures} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

static struct gs_block_class gstype_descr = {
    /* evaluator = */ gsnoeval,
    /* descrption = */ "GSBC Type Representation",
};
static void *gstype_nursury;

static void *gstype_alloc(ulong);

static int gstypes_size_item(struct gsbc_item);
static int gstypes_size_defn(struct gsbc_item);

void
gstypes_alloc_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, struct gstype **defns, int n)
{
    uint total_size,
        sizes[MAX_ITEMS_PER_SCC], offsets[MAX_ITEMS_PER_SCC],
        defn_sizes[MAX_ITEMS_PER_SCC], defn_offsets[MAX_ITEMS_PER_SCC]
    ;
    int i, align;
    void *base;

    align = sizeof(types[0]->node);

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    total_size = 0;
    for (i = 0; i < n; i++) {
        sizes[i] = gstypes_size_item(items[i]);
        defn_sizes[i] = gstypes_size_defn(items[i]);
        if (sizes[i] % align)
            sizes[i] += (align - sizes[i] % align) 
        ;
        if (defn_sizes[i] % align)
            defn_sizes[i] += (align - defn_sizes[i] % align) 
        ;
        offsets[i] = sizes[i] ? total_size : 0;
        defn_offsets[i] = defn_sizes[i] ? total_size + sizes[i] : 0;
        
        total_size += sizes[i] + defn_sizes[i];
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gstype_alloc(total_size);

    for (i = 0; i < n; i++) {
        if (sizes[i]) {
            types[i] = (struct gstype *)((uchar*)base + offsets[i]);
            types[i]->node = gstype_uninitialized;
            gssymtable_set_type(symtable, items[i].v->label, types[i]);
        } else {
            types[i] = 0;
        }
        if (defn_sizes[i]) {
            defns[i] = (struct gstype *)((uchar*)base + defn_offsets[i]);
            defns[i]->node = gstype_uninitialized;
            gssymtable_set_abstype(symtable, items[i].v->label, defns[i]);
        } else {
            defns[i] = 0;
        }
    }
}

static
int
gstypes_size_item(struct gsbc_item item)
{
    switch (item.type) {
        case gssymtypelable:
            if (gssymeq(item.v->directive, gssymtypedirective, ".tydefinedprim")) {
                return sizeof(struct gstype_prim);
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyexpr")) {
                return sizeof(struct gstype_indirection);
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyabstract")) {
                return sizeof(struct gstype_abstract);
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tycoercion")) {
                return sizeof(struct gstype_indirection);
            } else {
                gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "size %s type items", item.v->directive->name);
            }
        case gssymcoercionlable:
        case gssymdatalable:
        case gssymcodelable:
            return 0;
        default:
            gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "size type item of type %d", item.type);
    }
    return 0;
}

static
int
gstypes_size_defn(struct gsbc_item item)
{
    switch (item.type) {
        case gssymtypelable:
            if (gssymeq(item.v->directive, gssymtypedirective, ".tydefinedprim")) {
                return 0;
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyexpr")) {
                return 0;
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyabstract")) {
                return sizeof(struct gstype_indirection);
            } else if (gssymeq(item.v->directive, gssymtypedirective, ".tycoercion")) {
                return 0;
            } else {
                gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "size abstype defn for %s type items", item.v->directive->name);
            }
        case gssymdatalable:
        case gssymcodelable:
        case gssymcoercionlable:
            return 0;
        default:
            gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "size abstype defn for item of type %d", item.type);
    }
    return 0;
}

static void gstype_compile_type_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gstype *);
static void gstype_compile_coercion_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gstype *);

void
gstypes_compile_types(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **ptypes, struct gstype **defns, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedfile_segment *pseg;
                    struct gsparsedline *ptype;
                    struct gstype *res;

                    pseg = item.pseg;
                    ptype = item.v;
                    res = ptypes[i];

                    res->file = ptype->file;
                    res->lineno = ptype->lineno;

                    if (gssymeq(item.v->directive, gssymtypedirective, ".tyabstract")) {
                        struct gstype *defn;
                        struct gstype_abstract *abs;

                        res->node = gstype_abstract;
                        abs = (struct gstype_abstract *)res;
                        abs->name = item.v->label;
                        if (item.v->numarguments > 0)
                            abs->kind = gskind_compile(ptype, ptype->arguments[0]);
                        else
                            abs->kind = 0;

                        defn = defns[i];

                        gstype_compile_type_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), defn);
                    } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyexpr")) {
                        gstype_compile_type_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), res);
                    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tydefinedprim")) {
                        struct gstype_prim *prim;

                        res->node = gstype_prim;
                        prim = (struct gstype_prim *)res;
                        prim->primset = gsprims_lookup_prim_set(ptype->arguments[0]->name);
                        if (!prim->primset)
                            gswarning("%s:%d: Warning: Unknown prim set %s",
                                ptype->file->name,
                                ptype->lineno,
                                ptype->arguments[0]->name
                            )
                        ;
                        prim->name = ptype->arguments[1];
                        prim->kind = gskind_compile(ptype, ptype->arguments[2]);
                    } else if (gssymeq(item.v->directive, gssymtypedirective, ".tycoercion")) {
                        gstype_compile_coercion_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), res);
                    } else {
                        gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "gstypes_compile_types(%s)", item.v->directive->name);
                    }
                }
                break;
            case gssymdatalable:
            case gssymcodelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "gstypes_compile_types(type = %d)", item.type);
        }
    }
}

#define MAX_REGISTERS 0x100

struct gstype_compile_type_ops_closure {
    int nregs;
    gsinterned_string regs[MAX_REGISTERS];
    struct gstype *regvalues[MAX_REGISTERS];
    struct gsfile_symtable *symtable;
    struct gsparsedfile_segment **ppseg;
    enum {
        regglobal,
        regarg,
        regforall,
        reglet,
    } regclass;
};

static struct gstype *gstype_compile_type_ops_worker(struct gstype_compile_type_ops_closure *, struct gsparsedline *);

static
void
gstype_compile_type_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gstype *res)
{
    struct gstype_compile_type_ops_closure cl;
    struct gstype_indirection *indir;

    cl.nregs = 0;
    cl.symtable = symtable;
    cl.ppseg = ppseg;
    cl.regclass = regglobal;

    res->node = gstype_indirection;
    indir = (struct gstype_indirection *)res;

    indir->referent = gstype_compile_type_ops_worker(&cl, p);
}

struct gstype *
gstypes_compile_indir(gsinterned_string file , int lineno, struct gstype *referent)
{
    struct gstype *res;
    struct gstype_indirection *indir;

    res = gstype_alloc(sizeof(struct gstype_indirection));
    indir = (struct gstype_indirection *)res;

    res->node = gstype_indirection;
    res->file = file;
    res->lineno = lineno;
    indir->referent = referent;

    return res;
}

static struct gstype *gstype_compile_type_or_coercion_op(struct gstype_compile_type_ops_closure *, struct gsparsedline *, struct gstype *(*)(struct gstype_compile_type_ops_closure *, struct gsparsedline *));

static
struct gstype *
gstype_compile_type_ops_worker(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    int i, j;
    struct gstype *res;

    if (res = gstype_compile_type_or_coercion_op(cl, p, gstype_compile_type_ops_worker)) {
        return res;
    } else if (gssymeq(p->directive, gssymtypeop, ".tylambda")) {
        struct gskind *kind;
        struct gstype_lambda *lambda;

        if (cl->nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
        if (cl->regclass > regarg)
            gsfatal_bad_input(p, "Too late to add type arguments")
        ;
        cl->regclass = regarg;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->file, p->lineno, p->label, kind);
        cl->nregs++;
        res = gstype_alloc(sizeof(struct gstype_lambda));
        lambda = (struct gstype_lambda*)res;
        res->node = gstype_lambda;
        res->file = p->file;
        res->lineno = p->lineno;
        lambda->var = p->label;
        lambda->kind = kind;
        lambda->body = gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
        return res;
    } else if (gssymeq(p->directive, gssymtypeop, ".tyforall")) {
        struct gskind *kind;
        struct gstype_forall *forall;

        if (cl->nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
        if (cl->regclass > regforall)
            gsfatal_bad_input(p, "Too late to add forall arguments")
        ;
        cl->regclass = regforall;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->file, p->lineno, p->label, kind);
        cl->nregs++;
        res = gstype_alloc(sizeof(struct gstype_forall));
        forall = (struct gstype_forall*)res;
        res->node = gstype_forall;
        res->file = p->file;
        res->lineno = p->lineno;
        forall->var = p->label;
        forall->kind = kind;
        forall->body = gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
        return res;
    } else if (gssymeq(p->directive, gssymtypeop, ".tylet")) {
        struct gstype *reg;

        if (cl->nregs >= MAX_REGISTERS)
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
        ;
        if (cl->regclass > reglet)
            gsfatal_bad_input(p, "Too late to add lets")
        ;
        cl->regclass = reglet;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "base");
        reg = 0;
        for (i = 0; i < cl->nregs; i++) {
            if (cl->regs[i] == p->arguments[0]) {
                reg = cl->regvalues[i];
                goto have_register_for_let_base;
            }
        }
        gsfatal_bad_input(p, "Couldn't find base of let %s", p->arguments[0]->name);
    have_register_for_let_base:
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = reg;
            arg = 0;
            for (j = 0; j < cl->nregs; j++) {
                if (cl->regs[j] == p->arguments[i]) {
                    arg = cl->regvalues[j];
                    goto have_register_for_let_arg;
                }
            }
            gsfatal_bad_input(p, "Couldn't find argument for let %s", p->arguments[i]->name);
        have_register_for_let_arg:
            reg = gstype_supply(p->file, p->lineno, fun, arg);
        }
        cl->regvalues[cl->nregs] = reg;
        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else if (gssymeq(p->directive, gssymtypeop, ".tylift")) {
        return gstypes_compile_lift(p->file, p->lineno, gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p)));
    } else if (gssymeq(p->directive, gssymtypeop, ".tyref")) {
        struct gstype *reg;

        gsargcheck(p, 0, "referent");
        reg = 0;
        for (i = 0; i < cl->nregs; i++) {
            if (p->arguments[0] == cl->regs[i]) {
                reg = cl->regvalues[i];
                goto have_referent_register;
            }
        }
        gsfatal_bad_input(p, "Cannot find register %s", p->arguments[0]->name);
    have_referent_register:
        res = reg;
        for (i = 0; 1 + i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = res;
            arg = 0;
            for (j = 0; j < cl->nregs; j++) {
                if (p->arguments[1 + i] == cl->regs[j]) {
                    arg = cl->regvalues[j];
                    goto have_arg_register;
                }
            }
            gsfatal_bad_input(p, "Cannot find register %s", p->arguments[1 + i]->name);
        have_arg_register:
            res = gstype_apply(p->file, p->lineno, fun, arg);
        }
        return res;
    } else if (gssymeq(p->directive, gssymtypeop, ".tysum")) {
        struct gstype_constr constrs[MAX_REGISTERS];
        int i;
        int nconstrs;

        if (p->numarguments % 2)
            gsfatal_bad_input(p, "Cannot have odd number of arguments to .tysum")
        ;
        nconstrs = p->numarguments / 2;

        if (nconstrs > MAX_REGISTERS)
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "sums with more than 0x%x constructors", MAX_REGISTERS)
        ;

        for (i = 0; i < p->numarguments; i += 2) {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "constructors");
        }

        return gstypes_compile_sumv(p->file, p->lineno, nconstrs, constrs);
    } else if (gssymeq(p->directive, gssymtypeop, ".typroduct")) {
        struct gstype_product *prod;
        int numfields;

        if (p->numarguments % 2)
                gsfatal_bad_input(p, "Cannot have odd number of arguments to .tysum");
        numfields = p->numarguments / 2;
        res = gstype_alloc(sizeof(struct gstype_product) + numfields * sizeof(struct gstype_field));
        prod = (struct gstype_product *)res;
        res->node = gstype_product;
        res->file = p->file;
        res->lineno = p->lineno;
        prod->numfields = numfields;
        for (i = 0; i < p->numarguments; i += 2) {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Non-empty products");
        }
        return res;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gstype_compile_type_ops %s", p->directive->name);
    }
    return 0;
}

struct gstype *
gstypes_compile_lambda(gsinterned_string file, int lineno, gsinterned_string var, struct gskind *kind, struct gstype *body)
{
    struct gstype *res;
    struct gstype_lambda *lambda;

    res = gstype_alloc(sizeof(struct gstype_lambda));
    lambda = (struct gstype_lambda *)res;

    res->node = gstype_lambda;
    res->file = file;
    res->lineno = lineno;
    lambda->var = var;
    lambda->kind = kind;
    lambda->body = body;

    return res;
}

struct gstype *
gstypes_compile_lift(gsinterned_string file, int lineno, struct gstype *arg)
{
    struct gstype *res;
    struct gstype_lift *lift;

    res = gstype_alloc(sizeof(struct gstype_lift));
    lift = (struct gstype_lift *)res;

    res->node = gstype_lift;
    res->file = file;
    res->lineno = lineno;
    lift->arg = arg;

    return res;
}

struct gstype *
gstypes_compile_sum(gsinterned_string file, int lineno, int nconstrs, ...)
{
    va_list arg;
    struct gstype_constr constrs[MAX_REGISTERS];
    int i;

    if (nconstrs > MAX_REGISTERS)
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Sums with more than 0x%x constructors", file->name, lineno, MAX_REGISTERS)
    ;

    va_start(arg, nconstrs);
    for (i = 0; i < nconstrs; i++) {
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Copy constructors out of gstypes_compile_sumv arguments", file->name, lineno);
    }
    va_end(arg);

    return gstypes_compile_sumv(file, lineno, nconstrs, constrs);
}

struct gstype *
gstypes_compile_sumv(gsinterned_string file, int lineno, int nconstrs, struct gstype_constr *constrs)
{
    struct gstype *res;
    struct gstype_sum *sum;
    int i;

    res = gstype_alloc(sizeof(struct gstype_sum) + nconstrs * sizeof(struct gstype_constr));
    sum = (struct gstype_sum *)res;

    res->node = gstype_sum;
    res->file = file;
    res->lineno = lineno;
    sum->numconstrs = nconstrs;
    for (i = 0; i < nconstrs; i ++) {
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: set constructors", file->name, lineno);
    }

    return res;
}

struct gstype *
gstypes_compile_fun(gsinterned_string file, int lineno, struct gstype *tyarg, struct gstype *tyres)
{
    struct gstype *res;
    struct gstype_fun *fun;

    res = gstype_alloc(sizeof(struct gstype_fun));
    fun = (struct gstype_fun *)res;

    res->node = gstype_fun;
    res->file = file;
    res->lineno = lineno;
    fun->tyarg = tyarg;
    fun->tyres = tyres;

    return res;
}

static struct gstype *gstype_compile_coercion_ops_worker(struct gstype_compile_type_ops_closure *, struct gsparsedline *);

static
void
gstype_compile_coercion_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gstype *res)
{
    struct gstype_compile_type_ops_closure cl;
    struct gstype_indirection *indir;

    cl.nregs = 0;
    cl.symtable = symtable;
    cl.ppseg = ppseg;
    cl.regclass = regglobal;

    res->node = gstype_indirection;
    indir = (struct gstype_indirection *)res;

    indir->referent = gstype_compile_coercion_ops_worker(&cl, p);
}

static
struct gstype *
gstype_compile_coercion_ops_worker(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    int i, j;
    struct gstype *res;

    if (res = gstype_compile_type_or_coercion_op(cl, p, gstype_compile_coercion_ops_worker)) {
        return res;
    } else if (gssymeq(p->directive, gssymtypeop, ".tydefinition")) {
        struct gstype_coerce_definition *defn;
        int numargs;

        numargs = p->numarguments - 1;
        res = gstype_alloc(sizeof(struct gstype_coerce_definition) + numargs * sizeof(struct gstype *));
        defn = (struct gstype_coerce_definition *)res;
        res->node = gstype_coerce_definition;
        res->file = p->file;
        res->lineno = p->lineno;

        for (j = 0; j < cl->nregs; j++) {
            if (cl->regs[j] == p->arguments[0]) {
                defn->dest = cl->regvalues[j];
                goto have_register_for_destn;
            }
        }
        gsfatal_bad_input(p, "Couldn't find register %s", p->arguments[0]->name);
    have_register_for_destn:
        switch (defn->dest->node) {
            case gstype_abstract: {
                struct gstype_abstract *abs;

                abs = (struct gstype_abstract *)defn->dest;

                defn->source = gssymtable_get_abstype(cl->symtable, abs->name);
                if (!defn->source)
                    gsfatal_bad_input(p, "Couldn't find definition of abstract type %s", abs->name->name);

                goto have_defn_for_source;
            }
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, p, ".tydefinition with source a %d", defn->dest->node);
        }
    have_defn_for_source:
        defn->numargs = numargs;
        for (i = 1; i < p->numarguments; i ++) {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Arguments to .tydefinition");
        }
        return res;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gstype_compile_coercion_ops_worker %s", p->directive->name);
    }
    return 0;
}

static
struct gstype *
gstype_compile_type_or_coercion_op(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p, struct gstype *(*next)(struct gstype_compile_type_ops_closure *, struct gsparsedline *))
{
# if 0
    struct gstype *res;
#endif

    if (gssymeq(p->directive, gssymtypeop, ".tygvar")) {
        if (cl->nregs >= MAX_REGISTERS)
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
        ;
        if (cl->regclass > regglobal)
            gsfatal_bad_input(p, "Too late to add type globals")
        ;
        cl->regclass = regglobal;
        cl->regs[cl->nregs] = p->label;
        cl->regvalues[cl->nregs] = gssymtable_get_type(cl->symtable, p->label);
        if (!cl->regvalues[cl->nregs])
            gsfatal_bad_input(p, "Couldn't find referent %s", p->label->name)
        ;
        cl->nregs++;
        return next(cl, gsinput_next_line(cl->ppseg, p));
    } else {
        return 0;
    }
}

struct gstype *
gstypes_compile_type_var(gsinterned_string file, int lineno, gsinterned_string name, struct gskind *ky)
{
    struct gstype *res;
    struct gstype_var *var;

    res = gstype_alloc(sizeof(struct gstype_var));
    var = (struct gstype_var *)res;

    res->node = gstype_var;
    res->file = file;
    res->lineno = lineno;

    var->name = name;
    var->kind = ky;

    return res;
}

struct gstype *
gstype_apply(gsinterned_string file, int lineno, struct gstype *fun, struct gstype *arg)
{
    struct gstype *res;
    struct gstype_app *app;

    res = gstype_alloc(sizeof(struct gstype_app));
    res->node = gstype_app;
    res->file = file;
    res->lineno = lineno;
    app = (struct gstype_app *)res;
    app->fun = fun;
    app->arg = arg;

    return res;
}

static struct gstype *gstypes_subst(gsinterned_string, int, struct gstype *, gsinterned_string, struct gstype *);

struct gstype *
gstype_supply(gsinterned_string file, int lineno, struct gstype *fun, struct gstype *arg)
{
    switch (fun->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;
            indir = (struct gstype_indirection *)fun;
            return gstype_supply(file, lineno, indir->referent, arg);
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda;
            struct gskind *argkind;

            lambda = (struct gstype_lambda *)fun;

            argkind = gstypes_calculate_kind(arg);

            gstypes_kind_check(file, lineno, argkind, lambda->kind);

            return gstypes_subst(file, lineno, lambda->body, lambda->var, arg);
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, fun, "supply (node = %d)", fun->node);
    }
    return 0;
}

static
struct gstype *
gstypes_subst(gsinterned_string file, int lineno, struct gstype *type, gsinterned_string varname, struct gstype *type1)
{
    char buf[0x100];
    int i;

    switch (type->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)type;
            return gstypes_subst(file, lineno, indir->referent, varname, type1);
        }
        case gstype_abstract:
            return type;
        case gstype_prim:
            return type;
        case gstype_var: {
            struct gstype_var *var;

            var = (struct gstype_var *)type;

            if (var->name == varname)
                return type1;
            else
                return type;
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda, *reslambda;
            struct gstype *resbody;
            gsinterned_string nvar;
            struct gstype *res;
            int varno;

            lambda = (struct gstype_lambda *)type;

            if (lambda->var == varname)
                return type;

            resbody = lambda->body;
            nvar = lambda->var;
            varno = 0;
            while (gstypes_is_ftyvar(nvar, type1) || gstypes_is_ftyvar(nvar, type)) {
                if (seprint(buf, buf + sizeof(buf), "%s:%d", lambda->var->name, varno) >= buf + sizeof(buf))
                    gsfatal_unimpl(__FILE__, __LINE__, "Buffer overflow printing %s:%d", lambda->var->name, varno)
                ;
                nvar = gsintern_string(gssymtypelable, buf);
            }
            if (nvar != lambda->var)
                resbody = gstypes_subst(file, lineno, resbody, lambda->var, gstypes_compile_type_var(file, lineno, nvar, lambda->kind))
            ;
            resbody = gstypes_subst(file, lineno, resbody, varname, type1);
            res = gstype_alloc(sizeof(struct gstype_lambda));
            reslambda = (struct gstype_lambda *)res;
            res->node = gstype_lambda;
            res->file = type->file;
            res->lineno = type->lineno;
            reslambda->var = nvar;
            reslambda->kind = lambda->kind;
            reslambda->body = resbody;
            return res;
        }
        case gstype_lift: {
            struct gstype_lift *lift, *reslift;
            struct gstype *res;

            lift = (struct gstype_lift *)type;
            res = gstype_alloc(sizeof (struct gstype_lift));
            reslift = (struct gstype_lift *)res;
            res->node = gstype_lift;
            res->file = type->file;
            res->lineno = type->lineno;
            reslift->arg = gstypes_subst(file, lineno, lift->arg, varname, type1);

            return res;
        }
        case gstype_app: {
            struct gstype_app *app, *resapp;
            struct gstype *res;

            app = (struct gstype_app *)type;
            res = gstype_alloc(sizeof(struct gstype_app));
            resapp = (struct gstype_app *)res;
            res->node = gstype_app;
            res->file = type->file;
            res->lineno = type->lineno;
            resapp->fun = gstypes_subst(file, lineno, app->fun, varname, type1);
            resapp->arg = gstypes_subst(file, lineno, app->arg, varname, type1);

            return res;
        }
        case gstype_sum: {
            struct gstype_sum *sum, *ressum;
            struct gstype *res;

            sum = (struct gstype_sum *)type;
            res = gstype_alloc(sizeof(struct gstype_sum) + sum->numconstrs * sizeof(struct gstype_constr));
            ressum = (struct gstype_sum *)res;
            res->node = gstype_sum;
            res->file = type->file;
            res->lineno = type->lineno;
            ressum->numconstrs = sum->numconstrs;
            for (i = 0; i < sum->numconstrs; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "subst into constr arg type");
            }

            return res;
        }
        case gstype_product: {
            struct gstype_product *prod, *resprod;
            struct gstype *res;

            prod = (struct gstype_product *)type;
            res = gstype_alloc(sizeof(struct gstype_product) + prod->numfields * sizeof(struct gstype_field));
            resprod = (struct gstype_product *)res;
            res->node = gstype_product;
            res->file = type->file;
            res->lineno = type->lineno;
            resprod->numfields = prod->numfields;
            for (i = 0; i < prod->numfields; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "subst into field type");
            }

            return res;
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "subst (node = %d)", type->node);
    }
    return 0;
}

int
gstypes_is_ftyvar(gsinterned_string varname, struct gstype *type)
{
    int i;

    switch (type->node) {
        case gstype_indirection: {
            struct gstype_indirection *indir;

            indir = (struct gstype_indirection *)type;
            return gstypes_is_ftyvar(varname, indir->referent);
        }
        case gstype_prim: {
            return 0;
        }
        case gstype_var: {
            struct gstype_var *var;

            var = (struct gstype_var *)type;
            return var->name == varname;
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda;

            lambda = (struct gstype_lambda *)type;
            if (lambda->var == varname)
                return 0;
            return gstypes_is_ftyvar(varname, lambda->body);
        }
        case gstype_lift: {
            struct gstype_lift *lift;

            lift = (struct gstype_lift *)type;
            return gstypes_is_ftyvar(varname, lift->arg);
        }
        case gstype_sum: {
            struct gstype_sum *sum;

            sum = (struct gstype_sum *)type;
            for (i = 0; i < sum->numconstrs; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "tv (constr arg)");
            }

            return 0;
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, type, "fv (varname = %s, node = %d)", varname->name, type->node);
    }
    return 0;
}

static
void *
gstype_alloc(ulong size)
{
    return gs_sys_seg_suballoc(&gstype_descr, &gstype_nursury, size, sizeof(void*));
}

#define MAX_STACK_SIZE 0x100

struct gskind *
gskind_compile(struct gsparsedline *inpline, gsinterned_string ki)
{
    struct gskind *stack[MAX_STACK_SIZE], *base, *exp;
    int stacksize;
    char *p;

    stacksize = 0;

    for (p = ki->name; *p; p++) {
        switch (*p) {
            case '?':
                if (stacksize >= MAX_STACK_SIZE)
                    gsfatal_unimpl(__FILE__, __LINE__, "stack overflow")
                ;
                stack[stacksize++] = gskind_unknown_kind();
                break;
            case 'u':
                if (stacksize >= MAX_STACK_SIZE)
                    gsfatal_unimpl(__FILE__, __LINE__, "stack overflow")
                ;
                stack[stacksize++] = gskind_unlifted_kind();
                break;
            case '*':
                if (stacksize >= MAX_STACK_SIZE)
                    gsfatal_unimpl(__FILE__, __LINE__, "stack overflow")
                ;
                stack[stacksize++] = gskind_lifted_kind();
                break;
            case '^':
                if (stacksize < 2)
                    gsfatal_bad_input(inpline, "Invalid kind: Missing argument(s) to ^");
                exp = stack[--stacksize];
                base = stack[--stacksize];
                stack[stacksize++] = gskind_exponential_kind(base, exp);
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, inpline, "gskind_compile(%s)", p);
        }
    }

    if (stacksize < 1)
        gsfatal_bad_input(inpline, "Invalid kind: Stack empty at end of kind expression");
    if (stacksize > 1)
        gsfatal_bad_input(inpline, "Invalid kind: Stack still has %d entries at end of kind expression", stacksize);

    return stack[0];
}

struct gskind *
gstypes_compile_prim_kind(struct gsregistered_primkind *primky)
{
    switch (primky->node) {
        case gsprim_kind_unlifted:
            return gskind_unlifted_kind();
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "gstypes_compile_prim_kind(node = %d)", primky->node);
    }

    return 0;
}

static struct gskind *gskind_alloc(int);

static struct gskind *gskind_unknown_kind_constant;

struct gskind *
gskind_unknown_kind()
{
    if (!gskind_unknown_kind_constant) {
        gskind_unknown_kind_constant = gskind_alloc(0);
        gskind_unknown_kind_constant->node = gskind_unknown;
    }

    return gskind_unknown_kind_constant;
}

static struct gskind *gskind_unlifted_kind_constant;

struct gskind *
gskind_unlifted_kind()
{
    if (!gskind_unlifted_kind_constant) {
        gskind_unlifted_kind_constant = gskind_alloc(0);
        gskind_unlifted_kind_constant->node = gskind_unlifted;
    }

    return gskind_unlifted_kind_constant;
}

static struct gskind *gskind_lifted_kind_constant;

struct gskind *
gskind_lifted_kind()
{
    if (!gskind_lifted_kind_constant) {
        gskind_lifted_kind_constant = gskind_alloc(0);
        gskind_lifted_kind_constant->node = gskind_lifted;
    }

    return gskind_lifted_kind_constant;
}

struct gskind *
gskind_exponential_kind(struct gskind *base, struct gskind *exp)
{
    struct gskind *res;

    res = gskind_alloc(2);
    res->node = gskind_exponential;
    res->args[0] = base;
    res->args[1] = exp;

    return res;
}

static struct gs_block_class gskind_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "GSBC Kind Representation",
};
static void *gskind_nursury;

static
struct gskind *
gskind_alloc(int nargs)
{
    struct gskind *res;

    res = gs_sys_seg_suballoc(&gskind_descr, &gskind_nursury, sizeof(*res) + nargs * sizeof(*res->args), sizeof(res->node));

    return res;
}
