/* §source.file{Compiling Types Into C Data Structures} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gsregtables.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

static struct gs_block_class gstype_descr = {
    /* evaluator = */ gsnoeval,
    /* descrption = */ "GSBC Type Representation",
};
static void *gstype_nursury;

static void *gstype_alloc(ulong);

static void gstypes_compile_type(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int *, int, int);

static struct gstype *gstype_compile_type_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbc_item *, struct gstype **, int *, int);
static struct gstype *gstype_compile_coercion_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbc_item *, struct gstype **, int *, int);

void
gstypes_compile_types(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, int n)
{
    int i;
    int compiling[MAX_ITEMS_PER_SCC];

    for (i = 0; i < n; i++) {
        compiling[i] = 0;
        types[i] = 0;
    }

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        if (types[i])
            continue
        ;
        switch (item.type) {
            case gssymtypelable:
                gstypes_compile_type(symtable, items, types, compiling, i, n);
                break;
            case gssymdatalable:
            case gssymcodelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_compile_types(type = %d)", item.v->pos, item.type);
        }
    }
}

static
void
gstypes_compile_type(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, int *compiling, int i, int n)
{
    static gsinterned_string gssymtyelimprim;

    struct gsbc_item item;
    struct gsparsedfile_segment *pseg;
    struct gsparsedline *ptype;

    compiling[i] = 1;

    item = items[i];
    pseg = item.pseg;
    ptype = item.v;

    if (gssymeq(item.v->directive, gssymtypedirective, ".tyabstract")) {
        struct gskind *kind;

        kind = item.v->numarguments > 0 ? gskind_compile(ptype->pos, ptype->arguments[0]) : 0;
        types[i] = gstypes_compile_abstract(ptype->pos, item.v->label, kind);
    } else if (gssymeq(item.v->directive, gssymtypedirective, ".tyexpr")) {
        types[i] = gstype_compile_type_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), items, types, compiling, n);
    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tydefinedprim")) {
        struct gsregistered_primset *prims;

        if (prims = gsprims_lookup_prim_set(ptype->arguments[0]->name)) {
            struct gskind *kind;

            kind = gskind_compile(ptype->pos, ptype->arguments[2]);
            types[i] = gstypes_compile_knprim(ptype->pos, gsprim_type_defined, prims, ptype->arguments[1], kind);
        } else {
            gsfatal("%P: Unknown primset %s, which is bad because it supposedly contains defined primitive %s", ptype->pos, ptype->arguments[0]->name, ptype->arguments[1]->name);
        }
    } else if (gssymeq(ptype->directive, gssymtypedirective, ".tyapiprim")) {
        struct gsregistered_primset *prims;

        if (prims = gsprims_lookup_prim_set(ptype->arguments[0]->name)) {
            struct gskind *kind;

            kind = gskind_compile(ptype->pos, ptype->arguments[2]);
            types[i] = gstypes_compile_knprim(ptype->pos, gsprim_type_api, prims, ptype->arguments[1], kind);
        } else {
            struct gskind *kind;

            kind = gskind_compile(ptype->pos, ptype->arguments[2]);
            types[i] = gstype_compile_unprim(ptype->pos, gsprim_type_api, ptype->arguments[0], ptype->arguments[1], kind);
        }
    } else if (gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")) {
        struct gsregistered_primset *prims;

        if (prims = gsprims_lookup_prim_set(ptype->arguments[0]->name)) {
            struct gskind *kind;

            kind = gskind_compile(ptype->pos, ptype->arguments[2]);
            types[i] = gstypes_compile_knprim(ptype->pos, gsprim_type_elim, prims, ptype->arguments[1], kind);
        } else {
            struct gskind *kind;

            kind = gskind_compile(ptype->pos, ptype->arguments[2]);
            types[i] = gstype_compile_unprim(ptype->pos, gsprim_type_elim, ptype->arguments[0], ptype->arguments[1], kind);
        }
    } else if (gssymeq(item.v->directive, gssymtypedirective, ".tycoercion")) {
        types[i] = gstype_compile_coercion_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), items, types, compiling, n);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_compile_types(%s)", item.v->pos, item.v->directive->name);
    }

    compiling[i] = 0;
    gssymtable_set_type(symtable, item.v->label, types[i]);
}

void
gstypes_compile_type_definitions(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **defns, int n)
{
    static gsinterned_string gssymtyabstract, gssymtydefinedprim, gssymtyapiprim, gssymtyelimprim, gssymtyexpr;
    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedfile_segment *pseg;
                    struct gsparsedline *ptype;

                    pseg = item.pseg;
                    ptype = item.v;

                    if (gssymceq(ptype->directive, gssymtyabstract, gssymtypedirective, ".tyabstract")) {
                        defns[i] = gstype_compile_type_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), 0, 0, 0, n);
                    } else if (
                        gssymceq(ptype->directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")
                        || gssymceq(ptype->directive, gssymtyapiprim, gssymtypedirective, ".tyapiprim")
                        || gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")
                        || gssymceq(ptype->directive, gssymtyexpr, gssymtypedirective, ".tyexpr")
                    ) {
                        continue;
                    } else {
                        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_compile_type_definitions(%s)", item.v->pos, item.v->directive->name);
                    }
                }
                gssymtable_set_abstype(symtable, item.v->label, defns[i]);
                break;
            case gssymdatalable:
            case gssymcodelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_compile_types(type = %d)", item.v->pos, item.type);
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
    struct gsbc_item *items;
    struct gstype **types;
    int *compiling;
    int scc_size;
};

static struct gstype *gstype_compile_type_ops_worker(struct gstype_compile_type_ops_closure *, struct gsparsedline *);

static
struct gstype *
gstype_compile_type_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbc_item *items, struct gstype **types, int *compiling, int n)
{
    struct gstype_compile_type_ops_closure cl;

    cl.nregs = 0;
    cl.symtable = symtable;
    cl.ppseg = ppseg;
    cl.regclass = regglobal;
    cl.items = items;
    cl.compiling = compiling;
    cl.types = types;
    cl.scc_size = n;

    return gstype_compile_type_ops_worker(&cl, p);
}

static struct gstype *gstype_compile_type_or_coercion_op(struct gstype_compile_type_ops_closure *, struct gsparsedline *, struct gstype *(*)(struct gstype_compile_type_ops_closure *, struct gsparsedline *));

static
struct gstype *
gstype_compile_type_ops_worker(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    static gsinterned_string gssymtylambda, gssymtyforall, gssymtylet, gssymtylift, gssymtyfun, gssymtyref, gssymtysum, gssymtyproduct, gssymtyubproduct;

    int i;
    struct gstype *res;

    if (res = gstype_compile_type_or_coercion_op(cl, p, gstype_compile_type_ops_worker)) {
        return res;
    } else if (gssymceq(p->directive, gssymtylambda, gssymtypeop, ".tylambda")) {
        struct gskind *kind;
        struct gstype_lambda *lambda;

        if (cl->nregs >= MAX_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Register overflow", p->pos)
            ;
        if (cl->regclass > regarg)
            gsfatal_bad_input(p, "Too late to add type arguments")
        ;
        cl->regclass = regarg;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p->pos, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
        cl->nregs++;
        res = gstype_alloc(sizeof(struct gstype_lambda));
        lambda = (struct gstype_lambda*)res;
        res->node = gstype_lambda;
        res->pos = p->pos;
        lambda->var = p->label;
        lambda->kind = kind;
        lambda->body = gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
        return res;
    } else if (gssymceq(p->directive, gssymtyforall, gssymtypeop, ".tyforall")) {
        struct gskind *kind;

        if (cl->nregs >= MAX_REGISTERS)
                gsfatal_unimpl(__FILE__, __LINE__, "%P: Register overflow", p->pos)
            ;
        if (cl->regclass > regforall)
            gsfatal_bad_input(p, "Too late to add forall arguments")
        ;
        cl->regclass = regforall;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p->pos, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
        cl->nregs++;
        return gstypes_compile_forall(p->pos, p->label, kind,
            gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p))
        );
    } else if (gssymceq(p->directive, gssymtylet, gssymtypeop, ".tylet")) {
        struct gstype *reg;

        if (cl->nregs >= MAX_REGISTERS)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Register overflow", p->pos)
        ;
        if (cl->regclass > reglet)
            gsfatal_bad_input(p, "Too late to add lets")
        ;
        cl->regclass = reglet;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "base");
        reg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = reg;
            arg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i])];
            reg = gstype_supply(p->pos, fun, arg);
        }
        cl->regvalues[cl->nregs] = reg;
        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else if (gssymceq(p->directive, gssymtylift, gssymtypeop, ".tylift")) {
        return gstypes_compile_lift(p->pos, gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p)));
    } else if (gssymceq(p->directive, gssymtyfun, gssymtypeop, ".tyfun")) {
        int argreg;

        gsargcheck(p, 0, "argument");
        argreg = gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0]);
        return gstypes_compile_fun(p->pos, cl->regvalues[argreg], gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p)));
    } else if (gssymceq(p->directive, gssymtyref, gssymtypeop, ".tyref")) {
        struct gstype *reg;

        gsargcheck(p, 0, "referent");
        reg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
        res = reg;
        for (i = 0; 1 + i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = res;
            arg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[1 + i])];
            res = gstype_apply(p->pos, fun, arg);
        }
        return res;
    } else if (gssymceq(p->directive, gssymtysum, gssymtypeop, ".tysum")) {
        struct gstype_constr constrs[MAX_REGISTERS];
        int i;
        int nconstrs;

        if (p->numarguments % 2)
            gsfatal_bad_input(p, "Cannot have odd number of arguments to .tysum")
        ;
        nconstrs = p->numarguments / 2;

        if (nconstrs > MAX_REGISTERS)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: sums with more than 0x%x constructors", p->pos, MAX_REGISTERS)
        ;

        for (i = 0; i < p->numarguments; i += 2) {
            struct gstype *argtype;

            constrs[i / 2].name = p->arguments[i];
            argtype = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i + 1])];
            if (!argtype)
                gsfatal("%P: %s doesn't seem to be a type register", p->pos, p->arguments[i + 1]->name)
            ;
            constrs[i / 2].argtype = argtype;
        }

        return gstypes_compile_sumv(p->pos, nconstrs, constrs);
    } else if (gssymceq(p->directive, gssymtyproduct, gssymtypeop, ".typroduct")) {
        struct gstype_product *prod;
        int numfields;

        if (p->numarguments % 2)
                gsfatal_bad_input(p, "Cannot have odd number of arguments to .tysum");
        numfields = p->numarguments / 2;
        res = gstype_alloc(sizeof(struct gstype_product) + numfields * sizeof(struct gstype_field));
        prod = (struct gstype_product *)res;
        res->node = gstype_product;
        res->pos = p->pos;
        prod->numfields = numfields;
        for (i = 0; i < p->numarguments; i += 2) {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Non-empty products", p->pos);
        }
        return res;
    } else if (gssymceq(p->directive, gssymtyubproduct, gssymtypeop, ".tyubproduct")) {
        struct gstype_ubproduct *prod;
        int numfields;

        if (p->numarguments % 2)
            gsfatal_bad_input(p, "Cannot have odd number of arguments to .tyubproduct")
        ;
        numfields = p->numarguments / 2;
        res = gstype_alloc(sizeof(struct gstype_ubproduct) + numfields * sizeof(struct gstype_field));
        prod = (struct gstype_ubproduct *)res;
        res->node = gstype_ubproduct;
        res->pos = p->pos;
        prod->numfields = numfields;
        for (i = 0; i < p->numarguments; i += 2) {
            struct gstype *fieldtype;

            prod->fields[i / 2].name = p->arguments[i];
            fieldtype = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i + 1])];
            if (!fieldtype)
                gsfatal("%P: %s doesn't seem to be a type register", p->pos, p->arguments[i + 1]->name)
            ;
            prod->fields[i / 2].type = fieldtype;
        }
        return res;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstype_compile_type_ops %s", p->pos, p->directive->name);
    }
    return 0;
}

struct gstype *
gstypes_compile_lambda(struct gspos pos, gsinterned_string var, struct gskind *kind, struct gstype *body)
{
    struct gstype *res;
    struct gstype_lambda *lambda;

    res = gstype_alloc(sizeof(struct gstype_lambda));
    lambda = (struct gstype_lambda *)res;

    res->node = gstype_lambda;
    res->pos = pos;
    lambda->var = var;
    lambda->kind = kind;
    lambda->body = body;

    return res;
}

struct gstype *
gstypes_compile_forall(struct gspos pos, gsinterned_string var, struct gskind *kind, struct gstype *body)
{
    struct gstype *res;
    struct gstype_forall *forall;

    res = gstype_alloc(sizeof(struct gstype_forall));
    forall = (struct gstype_forall*)res;

    res->node = gstype_forall;
    res->pos = pos;
    forall->var = var;
    forall->kind = kind;
    forall->body = body;

    return res;
}

struct gstype *
gstypes_compile_lift(struct gspos pos, struct gstype *arg)
{
    struct gstype *res;
    struct gstype_lift *lift;

    res = gstype_alloc(sizeof(struct gstype_lift));
    lift = (struct gstype_lift *)res;

    res->node = gstype_lift;
    res->pos = pos;
    lift->arg = arg;

    return res;
}

struct gstype *
gstypes_compile_sum(struct gspos pos, int nconstrs, ...)
{
    va_list arg;
    struct gstype_constr constrs[MAX_REGISTERS];
    int i;

    if (nconstrs > MAX_REGISTERS)
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Sums with more than 0x%x constructors", pos, MAX_REGISTERS)
    ;

    va_start(arg, nconstrs);
    for (i = 0; i < nconstrs; i++) {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Copy constructors out of gstypes_compile_sumv arguments", pos);
    }
    va_end(arg);

    return gstypes_compile_sumv(pos, nconstrs, constrs);
}

struct gstype *
gstypes_compile_sumv(struct gspos pos, int nconstrs, struct gstype_constr *constrs)
{
    struct gstype *res;
    struct gstype_sum *sum;
    int i;

    res = gstype_alloc(sizeof(struct gstype_sum) + nconstrs * sizeof(struct gstype_constr));
    sum = (struct gstype_sum *)res;

    res->node = gstype_sum;
    res->pos = pos;
    sum->numconstrs = nconstrs;
    for (i = 0; i < nconstrs; i ++) {
        sum->constrs[i] = constrs[i];
    }

    return res;
}

struct gstype *
gstypes_compile_product(struct gspos pos, int nfields, ...)
{
    va_list arg;
    struct gstype_field fields[MAX_REGISTERS];
    int i;

    if (nfields > MAX_REGISTERS)
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Products with more than 0x%x fields", pos, MAX_REGISTERS)
    ;

    va_start(arg, nfields);
    for (i = 0; i < nfields; i++) {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Copy fields out of gstypes_compile_product arguments", pos);
    }
    va_end(arg);

    return gstype_compile_productv(pos, nfields, fields);
}

struct gstype *
gstype_compile_productv(struct gspos pos, int nfields, struct gstype_field *fields)
{
    struct gstype *res;
    struct gstype_product *product;
    int i;

    res = gstype_alloc(sizeof(struct gstype_product) + nfields * sizeof(struct gstype_field));
    product = (struct gstype_product *)res;

    res->node = gstype_product;
    res->pos = pos;
    product->numfields = nfields;
    for (i = 0; i < nfields; i++) {
        product->fields[i] = fields[i];
    }

    return res;
}

struct gstype *
gstypes_compile_fun(struct gspos pos, struct gstype *tyarg, struct gstype *tyres)
{
    struct gstype *res;
    struct gstype_fun *fun;

    res = gstype_alloc(sizeof(struct gstype_fun));
    fun = (struct gstype_fun *)res;

    res->node = gstype_fun;
    res->pos = pos;
    fun->tyarg = tyarg;
    fun->tyres = tyres;

    return res;
}

static struct gstype *gstype_compile_coercion_ops_worker(struct gstype_compile_type_ops_closure *, struct gsparsedline *);

static
struct gstype *
gstype_compile_coercion_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbc_item *items, struct gstype **types, int *compiling, int n)
{
    struct gstype_compile_type_ops_closure cl;

    cl.nregs = 0;
    cl.symtable = symtable;
    cl.ppseg = ppseg;
    cl.regclass = regglobal;
    cl.items = items;
    cl.compiling = compiling;
    cl.types = types;
    cl.scc_size = n;

    return gstype_compile_coercion_ops_worker(&cl, p);
}

static
struct gstype *
gstype_compile_coercion_ops_worker(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    int i;
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
        res->pos = p->pos;

        defn->dest = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
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
                gsfatal_unimpl(__FILE__, __LINE__, "%P: .tydefinition with source a %d", p->pos, defn->dest->node);
        }
    have_defn_for_source:
        defn->numargs = numargs;
        for (i = 1; i < p->numarguments; i ++) {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Arguments to .tydefinition", p->pos);
        }
        return res;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstype_compile_coercion_ops_worker %s", p->pos, p->directive->name);
    }
    return 0;
}

static
struct gstype *
gstype_compile_type_or_coercion_op(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p, struct gstype *(*next)(struct gstype_compile_type_ops_closure *, struct gsparsedline *))
{
    if (gssymeq(p->directive, gssymtypeop, ".tygvar")) {
        int i;

        if (cl->nregs >= MAX_REGISTERS)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Register overflow", p->pos)
        ;
        if (cl->regclass > regglobal)
            gsfatal_bad_input(p, "Too late to add type globals")
        ;
        cl->regclass = regglobal;
        cl->regs[cl->nregs] = p->label;
        cl->regvalues[cl->nregs] = gssymtable_get_type(cl->symtable, p->label);
        if (!cl->regvalues[cl->nregs] && cl->types) {
            for (i = 0; i < cl->scc_size; i++) {
                if (cl->items[i].v->label == p->label) {
                    if (cl->compiling[i])
                        gsfatal("%P: Loop: already compiling %s", p->pos, p->label->name)
                    ;
                    gstypes_compile_type(cl->symtable, cl->items, cl->types, cl->compiling, i, cl->scc_size);
                    cl->regvalues[cl->nregs] = cl->types[i];
                    break;
                }
            }
        }
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
gstypes_compile_abstract(struct gspos pos, gsinterned_string name, struct gskind *kind)
{
    struct gstype *res;
    struct gstype_abstract *abstract;

    res = gstype_alloc(sizeof(struct gstype_abstract));
    abstract = (struct gstype_abstract *)res;

    res->node = gstype_abstract;
    res->pos = pos;
    abstract->name = name;
    abstract->kind = kind;

    return res;
}

struct gstype *
gstypes_compile_prim(struct gspos pos, enum gsprim_type_group group, char *primsetname, char *primname, struct gskind *ky)
{
    struct gsregistered_primset *prims;

    if (prims = gsprims_lookup_prim_set(primsetname)) {
        return gstypes_compile_knprim(pos, group, prims, gsintern_string(gssymtypelable, primname), ky);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gstypes_compile_prim: unknown primset", pos);
        return 0;
    }
}

struct gstype *
gstypes_compile_knprim(struct gspos pos, enum gsprim_type_group group, struct gsregistered_primset *prims, gsinterned_string primname, struct gskind *ky)
{
    struct gstype *res;
    struct gstype_knprim *prim;

    res = gstype_alloc(sizeof(struct gstype_knprim));
    prim = (struct gstype_knprim *)res;

    res->node = gstype_knprim;
    res->pos = pos;
    prim->primtypegroup = group;
    prim->primset = prims;
    prim->primname = primname;
    prim->kind = ky;

    return res;
}

struct gstype *
gstype_compile_unprim(struct gspos pos, enum gsprim_type_group group, gsinterned_string primset, gsinterned_string primname, struct gskind *ky)
{
    struct gstype *res;
    struct gstype_unprim *prim;

    res = gstype_alloc(sizeof(struct gstype_unprim));
    prim = (struct gstype_unprim *)res;

    res->node = gstype_unprim;
    res->pos = pos;
    prim->primtypegroup = group;
    prim->primsetname = primset;
    prim->primname = primname;
    prim->kind = ky;

    return res;
}

struct gstype *
gstypes_compile_type_var(struct gspos pos, gsinterned_string name, struct gskind *ky)
{
    struct gstype *res;
    struct gstype_var *var;

    res = gstype_alloc(sizeof(struct gstype_var));
    var = (struct gstype_var *)res;

    res->node = gstype_var;
    res->pos = pos;

    var->name = name;
    var->kind = ky;

    return res;
}

struct gstype *
gstype_apply(struct gspos pos, struct gstype *fun, struct gstype *arg)
{
    struct gstype *res;
    struct gstype_app *app;

    gsbc_typecheck_check_boxed(pos, arg);

    res = gstype_alloc(sizeof(struct gstype_app));
    res->node = gstype_app;
    res->pos = pos;
    app = (struct gstype_app *)res;
    app->fun = fun;
    app->arg = arg;

    return res;
}

struct gstype *
gstype_supply(struct gspos pos, struct gstype *fun, struct gstype *arg)
{
    gsbc_typecheck_check_boxed(pos, arg);

    switch (fun->node) {
        case gstype_lambda: {
            struct gstype_lambda *lambda;
            struct gskind *argkind;

            lambda = (struct gstype_lambda *)fun;

            argkind = gstypes_calculate_kind(arg);

            gstypes_kind_check_fail(pos, argkind, lambda->kind);

            return gstypes_subst(pos, lambda->body, lambda->var, arg);
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, fun, "supply (node = %d)", fun->node);
    }
    return 0;
}

struct gstype *
gstype_instantiate(struct gspos pos, struct gstype *fun, struct gstype *arg)
{
    switch (fun->node) {
        case gstype_forall: {
            struct gstype_forall *forall;
            struct gskind *argkind;

            forall = (struct gstype_forall *)fun;

            argkind = gstypes_calculate_kind(arg);

            gstypes_kind_check_fail(pos, argkind, forall->kind);

            return gstypes_subst(pos, forall->body, forall->var, arg);
        }
        default:
            gsfatal_unimpl_type(__FILE__, __LINE__, fun, "supply (node = %d)", fun->node);
    }
    return 0;
}

struct gstype *
gstypes_subst(struct gspos pos, struct gstype *type, gsinterned_string varname, struct gstype *type1)
{
    char buf[0x100];
    int i;

    switch (type->node) {
        case gstype_abstract:
            return type;
        case gstype_knprim:
            return type;
        case gstype_unprim:
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
                resbody = gstypes_subst(pos, resbody, lambda->var, gstypes_compile_type_var(pos, nvar, lambda->kind))
            ;
            resbody = gstypes_subst(pos, resbody, varname, type1);
            res = gstype_alloc(sizeof(struct gstype_lambda));
            reslambda = (struct gstype_lambda *)res;
            res->node = gstype_lambda;
            res->pos = type->pos;
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
            res->pos = type->pos;
            reslift->arg = gstypes_subst(pos, lift->arg, varname, type1);

            return res;
        }
        case gstype_app: {
            struct gstype_app *app, *resapp;
            struct gstype *res;

            app = (struct gstype_app *)type;
            res = gstype_alloc(sizeof(struct gstype_app));
            resapp = (struct gstype_app *)res;
            res->node = gstype_app;
            res->pos = type->pos;
            resapp->fun = gstypes_subst(pos, app->fun, varname, type1);
            resapp->arg = gstypes_subst(pos, app->arg, varname, type1);

            return res;
        }
        case gstype_fun: {
            struct gstype_fun *fun, *resfun;
            struct gstype *res;

            fun = (struct gstype_fun *)type;
            res = gstype_alloc(sizeof(struct gstype_fun));
            resfun = (struct gstype_fun *)res;
            res->node = gstype_fun;
            res->pos = type->pos;
            resfun->tyarg = gstypes_subst(pos, fun->tyarg, varname, type1);
            resfun->tyres = gstypes_subst(pos, fun->tyres, varname, type1);

            return res;
        }
        case gstype_sum: {
            struct gstype_sum *sum, *ressum;
            struct gstype *res;

            sum = (struct gstype_sum *)type;
            res = gstype_alloc(sizeof(struct gstype_sum) + sum->numconstrs * sizeof(struct gstype_constr));
            ressum = (struct gstype_sum *)res;
            res->node = gstype_sum;
            res->pos = type->pos;
            ressum->numconstrs = sum->numconstrs;
            for (i = 0; i < sum->numconstrs; i++) {
                ressum->constrs[i].name = sum->constrs[i].name;
                ressum->constrs[i].argtype = gstypes_subst(pos, sum->constrs[i].argtype, varname, type1);
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
            res->pos = type->pos;
            resprod->numfields = prod->numfields;
            for (i = 0; i < prod->numfields; i++) {
                gsfatal_unimpl_type(__FILE__, __LINE__, type, "subst into field type");
            }

            return res;
        }
        case gstype_ubproduct: {
            struct gstype_ubproduct *prod, *resprod;
            struct gstype *res;

            prod = (struct gstype_ubproduct *)type;
            res = gstype_alloc(sizeof(struct gstype_ubproduct) + prod->numfields * sizeof(struct gstype_field));
            resprod = (struct gstype_ubproduct *)res;
            res->node = gstype_ubproduct;
            res->pos = type->pos;
            resprod->numfields = prod->numfields;
            for (i = 0; i < prod->numfields; i++) {
                resprod->fields[i].name = prod->fields[i].name;
                resprod->fields[i].type = gstypes_subst(pos, prod->fields[i].type, varname, type1);
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
        case gstype_knprim: {
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
        case gstype_app: {
            struct gstype_app *app;

            app = (struct gstype_app *)type;
            return gstypes_is_ftyvar(varname, app->fun) || gstypes_is_ftyvar(varname, app->arg);
        }
        case gstype_fun: {
            struct gstype_fun *fun;

            fun = (struct gstype_fun *)type;
            return gstypes_is_ftyvar(varname, fun->tyarg) || gstypes_is_ftyvar(varname, fun->tyres);
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
gskind_compile(struct gspos pos, gsinterned_string ki)
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
                    gsfatal("%P: Invalid kind: Missing argument(s) to ^", pos)
                ;
                exp = stack[--stacksize];
                base = stack[--stacksize];
                stack[stacksize++] = gskind_exponential_kind(base, exp);
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gskind_compile(%s)", pos, p);
        }
    }

    if (stacksize < 1)
        gsfatal("%P: Invalid kind: Stack empty at end of kind expression", pos);
    if (stacksize > 1)
        gsfatal("%P: Invalid kind: Stack still has %d entries at end of kind expression", pos, stacksize);

    return stack[0];
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
