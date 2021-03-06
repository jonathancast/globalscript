/* §source.file Compiling Types Into C Data Structures */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsregtables.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

static struct gs_sys_global_block_suballoc_info gstype_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* descrption = */ "GSBC Type Representation",
    },
};

static void *gstype_alloc(ulong);

static void gstypes_compile_type(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int *, int, int);

static struct gstype *gstype_compile_type_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbc_item *, struct gstype **, int *, int);

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
                gsfatal(UNIMPL("%P: gstypes_compile_types(type = %d)"), item.v->pos, item.type);
        }
    }
}

static
void
gstypes_compile_type(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, int *compiling, int i, int n)
{
    static gsinterned_string gssymtyabstract, gssymtyexpr, gssymtydefinedprim, gssymtyintrprim, gssymtyelimprim, gssymtyimpprim;

    struct gsbc_item item;
    struct gsparsedfile_segment *pseg;
    struct gsparsedline *ptype;

    compiling[i] = 1;

    item = items[i];
    pseg = item.pseg;
    ptype = item.v;

    if (gssymceq(item.v->directive, gssymtyabstract, gssymtypedirective, ".tyabstract")) {
        struct gskind *kind;

        kind = item.v->numarguments > 0 ? gskind_compile(ptype->pos, ptype->arguments[0]) : 0;
        types[i] = gstypes_compile_abstract(ptype->pos, item.v->label, kind);
    } else if (gssymceq(item.v->directive, gssymtyexpr, gssymtypedirective, ".tyexpr")) {
        types[i] = gstype_compile_type_ops(symtable, &pseg, gsinput_next_line(&pseg, ptype), items, types, compiling, n);
    } else if (
        gssymceq(ptype->directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")
        || gssymceq(ptype->directive, gssymtyintrprim, gssymtypedirective, ".tyintrprim")
        || gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")
        || gssymceq(ptype->directive, gssymtyimpprim, gssymtypedirective, ".tyimpprim")
    ) {
        struct gsregistered_primset *prims;
        struct gskind *kind;
        enum gsprim_type_group group;

        prims = gsprims_lookup_prim_set(ptype->arguments[0]->name);

        if (ptype->directive == gssymtydefinedprim && !prims)
            gsfatal("%P: Unknown primset %y, which is bad because it supposedly contains defined primtype %y", ptype->pos, ptype->arguments[0], ptype->arguments[1])
        ;

        if (ptype->directive == gssymtydefinedprim)
            group = gsprim_type_defined
        ; else if (ptype->directive == gssymtyintrprim)
            group = gsprim_type_intr
        ; else if (ptype->directive == gssymtyelimprim)
            group = gsprim_type_elim
        ; else if (ptype->directive == gssymtyimpprim)
            group = gsprim_type_imp
        ; else {
            group = -1;
            gsfatal(UNIMPL("%P: Get group for %y"), ptype->pos, ptype->directive);
        }
        kind = gskind_compile(ptype->pos, ptype->arguments[2]);
        types[i] = prims
            ? gstypes_compile_knprim(ptype->pos, group, prims, ptype->arguments[1], kind)
            : gstypes_compile_unprim(ptype->pos, group, ptype->arguments[0], ptype->arguments[1], kind)
        ;
    } else {
        gsfatal(UNIMPL("%P: gstypes_compile_types(%y)"), item.v->pos, item.v->directive);
    }

    compiling[i] = 0;
    gssymtable_set_type(symtable, item.v->label, types[i]);
}

void
gstypes_compile_type_definitions(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **defns, int n)
{
    static gsinterned_string gssymtyabstract, gssymtydefinedprim, gssymtyintrprim, gssymtyelimprim, gssymtyimpprim, gssymtyexpr;
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
                        gsbc_typecheck_check_boxed(ptype->pos, defns[i]);
                    } else if (
                        gssymceq(ptype->directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")
                        || gssymceq(ptype->directive, gssymtyintrprim, gssymtypedirective, ".tyintrprim")
                        || gssymceq(ptype->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")
                        || gssymceq(ptype->directive, gssymtyimpprim, gssymtypedirective, ".tyimpprim")
                        || gssymceq(ptype->directive, gssymtyexpr, gssymtypedirective, ".tyexpr")
                    ) {
                        defns[i] = 0;
                        continue;
                    } else {
                        gsfatal(UNIMPL("%P: gstypes_compile_type_definitions(%s)"), item.v->pos, item.v->directive->name);
                    }
                }
                gssymtable_set_abstype(symtable, item.v->label, defns[i]);
                break;
            case gssymdatalable:
            case gssymcodelable:
            case gssymcoercionlable:
                break;
            default:
                gsfatal(UNIMPL("%P: gstypes_compile_types(type = %d)"), item.v->pos, item.type);
        }
    }
}

struct gstype_compile_type_ops_closure {
    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    struct gstype *regvalues[MAX_NUM_REGISTERS];
    struct gsfile_symtable *symtable;
    struct gsparsedfile_segment **ppseg;
    enum {
        regglobal,
        regarg,
        regforall,
        regexists,
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

static struct gstype *gstype_compile_type_global_var_op(struct gstype_compile_type_ops_closure *, struct gsparsedline *);

struct gstype *
gstype_compile_type_ops_worker(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    static gsinterned_string gssymtylambda, gssymtyforall, gssymtyexists, gssymtylet, gssymtylift, gssymtyfun, gssymtyref, gssymtysum, gssymtyubsum, gssymtyproduct, gssymtyubproduct;

    int i;
    struct gstype *res;

    if (res = gstype_compile_type_global_var_op(cl, p)) {
        return res;
    } else if (gssymceq(p->directive, gssymtylambda, gssymtypeop, ".tylambda")) {
        struct gskind *kind;
        struct gstype_lambda *lambda;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regarg) gsfatal("%P: Too late to add type arguments", p->pos);
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

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regforall) gsfatal("%P: Too late to add forall arguments", p->pos);
        cl->regclass = regforall;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p->pos, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
        cl->nregs++;
        return gstypes_compile_forall(p->pos, p->label, kind,
            gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p))
        );
    } else if (gssymceq(p->directive, gssymtyexists, gssymtypeop, ".tyexists")) {
        struct gskind *kind;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regexists) gsfatal("%P: Too late to add forall arguments", p->pos);
        cl->regclass = regexists;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p->pos, p->arguments[0]);
        cl->regvalues[cl->nregs] = gstypes_compile_type_var(p->pos, p->label, kind);
        cl->nregs++;
        return gstypes_compile_exists(p->pos, p->label, kind,
            gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p))
        );
    } else if (gssymceq(p->directive, gssymtylet, gssymtypeop, ".tylet")) {
        struct gstype *reg;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > reglet) gsfatal("%P: Too late to add lets", p->pos);
        cl->regclass = reglet;
        cl->regs[cl->nregs] = p->label;
        gsargcheck(p, 0, "base");
        reg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = reg;
            arg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i])];
            reg = gstype_apply(p->pos, fun, arg);
        }
        cl->regvalues[cl->nregs] = reg;
        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else if (gssymceq(p->directive, gssymtylift, gssymtypeop, ".tylift")) {
        return gstypes_compile_lift(p->pos, gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p)));
    } else if (gssymceq(p->directive, gssymtyfun, gssymtypeop, ".tyfun")) {
        struct gstype *argty;

        gsargcheck(p, 0, "argument");
        argty = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *tyarg;

            tyarg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i])];
            argty = gstype_apply(p->pos, argty, tyarg);
        }
        return gstypes_compile_fun(p->pos, argty, gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p)));
    } else if (gssymceq(p->directive, gssymtyref, gssymtypeop, ".tyref")) {
        struct gstype *reg;

        gsargcheck(p, 0, "referent");
        reg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[0])];
        res = reg;
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *fun, *arg;

            fun = res;
            arg = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i])];
            res = gstype_apply(p->pos, fun, arg);
        }
        return res;
    } else if (gssymceq(p->directive, gssymtysum, gssymtypeop, ".tysum")) {
        struct gstype_constr constrs[MAX_NUM_REGISTERS];
        int i;
        int nconstrs;

        if (p->numarguments % 2)
            gsfatal("%P: Cannot have odd number of arguments to .tysum", p->pos)
        ;
        nconstrs = p->numarguments / 2;

        if (nconstrs > MAX_NUM_REGISTERS)
            gsfatal(UNIMPL("%P: sums with more than 0x%x constructors"), p->pos, MAX_NUM_REGISTERS)
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
    } else if (gssymceq(p->directive, gssymtyubsum, gssymtypeop, ".tyubsum")) {
        struct gstype_constr constrs[MAX_NUM_REGISTERS];
        int i;
        int nconstrs;

        if (p->numarguments % 2)
            gsfatal("%P: Cannot have odd number of arguments to .tyubsum", p->pos)
        ;
        nconstrs = p->numarguments / 2;

        if (nconstrs > MAX_NUM_REGISTERS)
            gsfatal(UNIMPL("%P: sums with more than 0x%x constructors"), p->pos, MAX_NUM_REGISTERS)
        ;

        for (i = 0; i < p->numarguments; i += 2) {
            struct gstype *argtype;

            constrs[i / 2].name = p->arguments[i];
            argtype = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i + 1])];
            if (!argtype)
                gsfatal("%P: %y doesn't seem to be a type register", p->pos, p->arguments[i + 1])
            ;
            constrs[i / 2].argtype = argtype;
        }

        return gstypes_compile_ubsumv(p->pos, nconstrs, constrs);
    } else if (gssymceq(p->directive, gssymtyproduct, gssymtypeop, ".typroduct")) {
        struct gstype_field fields[MAX_NUM_REGISTERS];
        int numfields;

        if (p->numarguments % 2)
            gsfatal("%P: Cannot have odd number of arguments to .typroduct", p->pos)
        ;
        numfields = p->numarguments / 2;
        for (i = 0; i < p->numarguments; i += 2) {
            struct gstype *fieldtype;

            fields[i / 2].name = p->arguments[i];
            fieldtype = cl->regvalues[gsbc_find_register(p, cl->regs, cl->nregs, p->arguments[i + 1])];
            if (!fieldtype)
                gsfatal("%P: %y doesn't seem to be a type register", p->pos, p->arguments[i + 1])
            ;
            fields[i / 2].type = fieldtype;
        }
        return gstypes_compile_productv(p->pos, numfields, fields);
    } else if (gssymceq(p->directive, gssymtyubproduct, gssymtypeop, ".tyubproduct")) {
        struct gstype_ubproduct *prod;
        int numfields;

        if (p->numarguments % 2)
            gsfatal("%P: Cannot have odd number of arguments to .tyubproduct", p->pos)
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
        gsfatal(UNIMPL("%P: gstype_compile_type_ops %y"), p->pos, p->directive);
    }
    return 0;
}

struct gstype *
gstype_compile_type_global_var_op(struct gstype_compile_type_ops_closure *cl, struct gsparsedline *p)
{
    static gsinterned_string gssymtygvar, gssymtyextabstype, gssymtyextelimprim, gssymtyextimpprim;

    if (gssymceq(p->directive, gssymtygvar, gssymtypeop, ".tygvar")) {
        int i;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regglobal) gsfatal("%P: Too late to add type globals", p->pos);
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
            gsfatal("%P: Couldn't find referent %y", p->pos, p->label)
        ;
        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else if (gssymceq(p->directive, gssymtyextabstype, gssymtypeop, ".tyextabstype")) {
        struct gskind *kind;
        struct gstype_abstract *abstype;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regglobal) gsfatal("%P: Too late to add type globals", p->pos);
        cl->regclass = regglobal;
        cl->regs[cl->nregs] = p->label;
        cl->regvalues[cl->nregs] = gssymtable_get_type(cl->symtable, p->label);
        if (!cl->regvalues[cl->nregs])
            gsfatal("%P: Couldn't find referent %y", p->pos, p->label)
        ;

        gsargcheck(p, 0, "kind");
        kind = gskind_compile(p->pos, p->arguments[0]);

        if (cl->regvalues[cl->nregs]->node != gstype_abstract)
            gsfatal("%P: Referent %y isn't actually an abstype", p->pos, p->label)
        ;
        abstype = (struct gstype_abstract *)cl->regvalues[cl->nregs];
        if (abstype->name != p->label)
            gsfatal("%P: Referent %y translates to differently named abstype %y", p->pos, p->label, abstype->name)
        ;
        gstypes_kind_check_fail(p->pos, abstype->kind, kind);

        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else if (
        gssymceq(p->directive, gssymtyextelimprim, gssymtypeop, ".tyextelimprim")
        || gssymceq(p->directive, gssymtyextimpprim, gssymtypeop, ".tyextimpprim")
    ) {
        struct gsregistered_primset *prims;
        struct gskind *kind;
        enum gsprim_type_group group;

        if (cl->nregs >= MAX_NUM_REGISTERS) gsfatal(UNIMPL("%P: Register overflow"), p->pos);
        if (cl->regclass > regglobal)
            gsfatal("%P: Too late to add type globals", p->pos)
        ;
        cl->regclass = regglobal;
        cl->regs[cl->nregs] = p->label;

        prims = gsprims_lookup_prim_set(p->arguments[0]->name);

        if (p->directive == 0 && !prims) /* > gssymtydefinedprim */
            gsfatal("%P: Unknown primset %y, which is bad because it supposedly contains defined primtype %y", p->pos, p->arguments[0], p->arguments[1])
        ;

        if (p->directive == 0) /* > gssymtydefinedprim */
            group = gsprim_type_defined
        ; else if (p->directive == 0) /* > gssymtyintrprim */
            group = gsprim_type_intr
        ; else if (p->directive == gssymtyextelimprim)
            group = gsprim_type_elim
        ; else if (p->directive == gssymtyextimpprim)
            group = gsprim_type_imp
        ; else {
            group = -1;
            gsfatal(UNIMPL("%P: Get group for %y"), p->pos, p->directive);
        }
        kind = gskind_compile(p->pos, p->arguments[2]);
        cl->regvalues[cl->nregs] = prims
            ? gstypes_compile_knprim(p->pos, group, prims, p->arguments[1], kind)
            : gstypes_compile_unprim(p->pos, group, p->arguments[0], p->arguments[1], kind)
        ;

        cl->nregs++;
        return gstype_compile_type_ops_worker(cl, gsinput_next_line(cl->ppseg, p));
    } else {
        return 0;
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
gstypes_compile_exists(struct gspos pos, gsinterned_string var, struct gskind *kind, struct gstype *body)
{
    struct gstype *res;
    struct gstype_exists *exists;

    res = gstype_alloc(sizeof(struct gstype_exists));
    exists = (struct gstype_exists*)res;

    res->node = gstype_exists;
    res->pos = pos;
    exists->var = var;
    exists->kind = kind;
    exists->body = body;

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
    struct gstype_constr constrs[MAX_NUM_REGISTERS];
    int i;

    if (nconstrs > MAX_NUM_REGISTERS)
        gsfatal(UNIMPL("%P: Sums with more than 0x%x constructors"), pos, MAX_NUM_REGISTERS)
    ;

    va_start(arg, nconstrs);
    for (i = 0; i < nconstrs; i++) {
        constrs[i].name = va_arg(arg, gsinterned_string);
        constrs[i].argtype = va_arg(arg, struct gstype *);
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
gstypes_compile_ubsumv(struct gspos pos, int nconstrs, struct gstype_constr *constrs)
{
    struct gstype *res;
    struct gstype_ubsum *sum;
    int i;

    res = gstype_alloc(sizeof(struct gstype_ubsum) + nconstrs * sizeof(struct gstype_constr));
    sum = (struct gstype_ubsum *)res;

    res->node = gstype_ubsum;
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
    struct gstype_field fields[MAX_NUM_REGISTERS];
    int i;

    if (nfields > MAX_NUM_REGISTERS)
        gsfatal(UNIMPL("%P: Products with more than 0x%x fields"), pos, MAX_NUM_REGISTERS)
    ;

    va_start(arg, nfields);
    for (i = 0; i < nfields; i++) {
        gsfatal(UNIMPL("%P: Copy fields out of gstypes_compile_product arguments"), pos);
    }
    va_end(arg);

    return gstypes_compile_productv(pos, nfields, fields);
}

struct gstype *
gstypes_compile_productv(struct gspos pos, int nfields, struct gstype_field *fields)
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
gstypes_compile_ubproduct(struct gspos pos, int nfields, ...)
{
    va_list arg;
    struct gstype_field fields[MAX_NUM_REGISTERS];
    int i;

    if (nfields > MAX_NUM_REGISTERS)
        gsfatal(UNIMPL("%P: Products with more than 0x%x fields"), pos, MAX_NUM_REGISTERS)
    ;

    va_start(arg, nfields);
    for (i = 0; i < nfields; i++) {
        fields[i].name = va_arg(arg, gsinterned_string);
        fields[i].type = va_arg(arg, struct gstype *);
    }
    va_end(arg);

    return gstypes_compile_ubproductv(pos, nfields, fields);
}

struct gstype *
gstypes_compile_ubproductv(struct gspos pos, int nfields, struct gstype_field *fields)
{
    struct gstype *res;
    struct gstype_ubproduct *product;
    int i;

    res = gstype_alloc(sizeof(struct gstype_ubproduct) + nfields * sizeof(struct gstype_field));
    product = (struct gstype_ubproduct *)res;

    res->node = gstype_ubproduct;
    res->pos = pos;
    product->numfields = nfields;
    for (i = 0; i < nfields; i++) product->fields[i] = fields[i];

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
        return gstypes_compile_unprim(pos, group, gsintern_string(gssymprimsetlable, primsetname), gsintern_string(gssymtypelable, primname), ky);
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
gstypes_compile_unprim(struct gspos pos, enum gsprim_type_group group, gsinterned_string primset, gsinterned_string primname, struct gskind *ky)
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
    gsbc_typecheck_check_boxed(pos, arg);

    switch (fun->node) {
        case gstype_abstract:
        case gstype_knprim:
        case gstype_unprim:
        case gstype_app: {
            struct gstype *res;
            struct gstype_app *app;

            res = gstype_alloc(sizeof(struct gstype_app));
            res->node = gstype_app;
            res->pos = pos;
            app = (struct gstype_app *)res;
            app->fun = fun;
            app->arg = arg;

            return res;
        }
        case gstype_lambda: {
            struct gstype_lambda *lambda;
            struct gskind *argkind;

            lambda = (struct gstype_lambda *)fun;

            argkind = gstypes_calculate_kind(arg);

            gstypes_kind_check_fail(pos, argkind, lambda->kind);

            return gstypes_subst(pos, lambda->body, lambda->var, arg);
        }
        case gstype_var:
        case gstype_product:
        case gstype_sum:
            gsfatal("%P: Too many arguments to %P", pos, fun->pos);
        default:
            gsfatal(UNIMPL("%P: gstype_apply(node = %d)"), pos, fun->node);
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
        default: {
            char buf[0x100];
            if (gstypes_eprint_type(buf, buf + sizeof(buf), fun) >= buf + sizeof(buf))
                gsfatal(UNIMPL("%P: supply (node = %d)"), fun->pos, fun->node)
            ;
            gsfatal("%P: Can't instantiate %s (%P), which isn't a polymorphic type", pos, buf, fun->pos);
        }
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
                    gsfatal(UNIMPL("Buffer overflow printing %s:%d"), lambda->var->name, varno)
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
        case gstype_forall: {
            struct gstype_forall *forall, *resforall;
            struct gstype *resbody;
            gsinterned_string nvar;
            struct gstype *res;
            int varno;

            forall = (struct gstype_forall *)type;

            if (forall->var == varname)
                return type;

            resbody = forall->body;
            nvar = forall->var;
            varno = 0;
            while (gstypes_is_ftyvar(nvar, type1) || gstypes_is_ftyvar(nvar, type)) {
                if (seprint(buf, buf + sizeof(buf), "%y%d", forall->var, varno) >= buf + sizeof(buf))
                    gsfatal(UNIMPL("Buffer overflow printing %y%d"), forall->var, varno)
                ;
                nvar = gsintern_string(gssymtypelable, buf);
            }
            if (nvar != forall->var)
                resbody = gstypes_subst(pos, resbody, forall->var, gstypes_compile_type_var(pos, nvar, forall->kind))
            ;
            resbody = gstypes_subst(pos, resbody, varname, type1);
            res = gstype_alloc(sizeof(struct gstype_forall));
            resforall = (struct gstype_forall *)res;
            res->node = gstype_forall;
            res->pos = type->pos;
            resforall->var = nvar;
            resforall->kind = forall->kind;
            resforall->body = resbody;
            return res;
        }
        case gstype_exists: {
            struct gstype_exists *exists, *resexists;
            struct gstype *resbody;
            gsinterned_string nvar;
            struct gstype *res;
            int varno;

            exists = (struct gstype_exists *)type;

            if (exists->var == varname)
                return type;

            resbody = exists->body;
            nvar = exists->var;
            varno = 0;
            while (gstypes_is_ftyvar(nvar, type1) || gstypes_is_ftyvar(nvar, type)) {
                if (seprint(buf, buf + sizeof(buf), "%y%d", exists->var, varno) >= buf + sizeof(buf))
                    gsfatal(UNIMPL("Buffer overflow printing %y%d"), exists->var, varno)
                ;
                nvar = gsintern_string(gssymtypelable, buf);
            }
            if (nvar != exists->var)
                resbody = gstypes_subst(pos, resbody, exists->var, gstypes_compile_type_var(pos, nvar, exists->kind))
            ;
            resbody = gstypes_subst(pos, resbody, varname, type1);
            res = gstype_alloc(sizeof(struct gstype_exists));
            resexists = (struct gstype_exists *)res;
            res->node = gstype_exists;
            res->pos = type->pos;
            resexists->var = nvar;
            resexists->kind = exists->kind;
            resexists->body = resbody;
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
        case gstype_ubsum: {
            struct gstype_ubsum *sum, *ressum;
            struct gstype *res;

            sum = (struct gstype_ubsum *)type;
            res = gstype_alloc(sizeof(struct gstype_ubsum) + sum->numconstrs * sizeof(struct gstype_constr));
            ressum = (struct gstype_ubsum *)res;
            res->node = gstype_ubsum;
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
                resprod->fields[i].name = prod->fields[i].name;
                resprod->fields[i].type = gstypes_subst(pos, prod->fields[i].type, varname, type1);
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
            gsfatal(UNIMPL("%P: subst (node = %d)"), type->pos, type->node);
    }
    return 0;
}

int
gstypes_is_ftyvar(gsinterned_string varname, struct gstype *type)
{
    int i;

    switch (type->node) {
        case gstype_abstract:
        case gstype_knprim:
        case gstype_unprim:
            return 0;
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
        case gstype_forall: {
            struct gstype_forall *forall;

            forall = (struct gstype_forall *)type;
            if (forall->var == varname)
                return 0
            ;
            return gstypes_is_ftyvar(varname, forall->body);
        }
        case gstype_exists: {
            struct gstype_exists *exists;

            exists = (struct gstype_exists *)type;
            if (exists->var == varname)
                return 0
            ;
            return gstypes_is_ftyvar(varname, exists->body);
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
            for (i = 0; i < sum->numconstrs; i++)
                if (gstypes_is_ftyvar(varname, sum->constrs[i].argtype))
                    return 1
            ;

            return 0;
        }
        case gstype_ubsum: {
            struct gstype_ubsum *sum;

            sum = (struct gstype_ubsum *)type;
            for (i = 0; i < sum->numconstrs; i++)
                if (gstypes_is_ftyvar(varname, sum->constrs[i].argtype))
                    return 1
            ;

            return 0;
        }
        case gstype_product: {
            struct gstype_product *product;

            product = (struct gstype_product *)type;
            for (i = 0; i < product->numfields; i++)
                if (gstypes_is_ftyvar(varname, product->fields[i].type))
                    return 1
            ;

            return 0;
        }
        case gstype_ubproduct: {
            struct gstype_ubproduct *product;

            product = (struct gstype_ubproduct *)type;
            for (i = 0; i < product->numfields; i++)
                if (gstypes_is_ftyvar(varname, product->fields[i].type))
                    return 1
            ;

            return 0;
        }
        default:
            gsfatal(UNIMPL("%P: fv (varname = %y, node = %d)"), type->pos, varname, type->node);
    }
    return 0;
}

static
void *
gstype_alloc(ulong size)
{
    return gs_sys_global_block_suballoc(&gstype_info, size);
}

#define MAX_STACK_SIZE 0x100

struct gskind *
gskind_compile_string(struct gspos pos, char *s)
{
    return gskind_compile(pos, gsintern_string(gssymkindexpr, s));
}

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
                if (stacksize >= MAX_STACK_SIZE) gsfatal(UNIMPL("stack overflow"));
                stack[stacksize++] = gskind_unknown_kind();
                break;
            case 'u':
                if (stacksize >= MAX_STACK_SIZE) gsfatal(UNIMPL("stack overflow"));
                stack[stacksize++] = gskind_unlifted_kind();
                break;
            case '*':
                if (stacksize >= MAX_STACK_SIZE) gsfatal(UNIMPL("stack overflow"));
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
                gsfatal(UNIMPL("%P: gskind_compile(%s)"), pos, p);
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

static struct gs_sys_global_block_suballoc_info gskind_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "GSBC Kind Representation",
    },
};

static
struct gskind *
gskind_alloc(int nargs)
{
    struct gskind *res;

    res = gs_sys_global_block_suballoc(&gskind_info, sizeof(*res) + nargs * sizeof(*res->args));

    return res;
}
