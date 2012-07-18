/* §source.file{Compiling Types Into C Data Structures} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gstypealloc.h"

static struct gs_block_class gstype_descr = {
    /* evaluator = */ gsnoeval,
    /* descrption = */ "GSBC Type Representation",
};
static void *gstype_nursury;

void
gstypes_alloc_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **types, int n)
{
    uint total_size, offsets[MAX_ITEMS_PER_SCC];
    int i, align, type_size;
    void *base;

    align = sizeof(types[0]->node);
    type_size = sizeof(struct gstype);
    if (type_size % align)
        type_size += (align - type_size % align) 
    ;

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    total_size = 0;
    for (i = 0; i < n; i++) {
        int size;

        offsets[i] = total_size;
        size = items[i].type == gssymtypelable ? type_size : 0;
        total_size += size;
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gs_sys_seg_suballoc(&gstype_descr, &gstype_nursury, total_size, align);

    for (i = 0; i < n; i++) {
        if (items[i].type == gssymtypelable) {
            types[i] = (struct gstype *)((uchar*)base + offsets[i]);
            types[i]->node = gstype_uninitialized;
            gssymtable_set_type(symtable, items[i].v.ptype->label, types[i]);
        }
    }
}

static struct gstype_expr_summary *gstype_compile_type_ops(struct gsfile_symtable *, struct gsparsedline *);

void
gstypes_compile_types(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gstype **ptypes, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        struct gsbc_item item;

        item = items[i];
        switch (item.type) {
            case gssymtypelable:
                {
                    struct gsparsedline *ptype;
                    struct gstype *res;

                    ptype = item.v.ptype;
                    res = ptypes[i];

                    res->file = ptype->file;
                    res->lineno = ptype->lineno;

                    if (gssymeq(item.v.ptype->directive, gssymtypedirective, ".tyabstract")) {
                        res->node = gstype_abstract;
                        res->a.expr = gstype_compile_type_ops(symtable, gsinput_next_line(ptype));
                    } else if (gssymeq(item.v.ptype->directive, gssymtypedirective, ".tyexpr")) {
                        res->node = gstype_expr;
                        res->a.expr = gstype_compile_type_ops(symtable, gsinput_next_line(ptype));
                    } else if (gssymeq(item.v.ptype->directive, gssymtypedirective, ".tydefinedprim")) {
                        res->node = gstype_prim;
                        res->a.prim.primset = gsprims_lookup_prim_set(ptype->arguments[0]->name);
                        if (!res->a.prim.primset)
                            gswarning("%s:%d: Warning: Unknown prim set %s",
                                ptype->file->name,
                                ptype->lineno,
                                ptype->arguments[0]->name
                            )
                        ;
                        res->a.prim.name = ptype->arguments[1];
                        res->a.prim.kind = gskind_compile(ptype, ptype->arguments[2]);
                    } else {
                        gsfatal_unimpl_input(__FILE__, __LINE__, item.v.ptype, "gstypes_compile_types(%s)", item.v.ptype->directive->name);
                    }
                }
                break;
            case gssymdatalable:
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, item.v.ptype, "gstypes_compile_types(type = %d)", item.type);
        }
    }
}

static struct gs_block_class gstype_expr_summary_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "GSBC Type Expr Summary",
};
static void *gstype_expr_summary_nursury;

static struct gs_block_class gstype_expr_descr = {
    /* evaluator = */ gsnoeval,
    /* descrption = */ "GSBC Type Expression",
};
static void *gstype_expr_nursury;

#define MAX_REGISTERS 0x100
#define MAX_CONTINUATIONS 0x100

#define PUSH_CONTINUATION \
    do { \
        if (nconts >= MAX_CONTINUATIONS) \
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Continuation stack overflow (max # of continuations %x)", MAX_CONTINUATIONS); \
        tyconts[nconts++] = p; \
    } while (0)

static
struct gstype_expr_summary *
gstype_compile_type_ops(struct gsfile_symtable *symtable, struct gsparsedline *p)
{
    gsinterned_string regs[MAX_REGISTERS], codelables[MAX_REGISTERS];
    struct gstype *tyglobals[MAX_REGISTERS];
    struct gstype *codelabledests[MAX_REGISTERS];
    struct gskind *fvkinds[MAX_REGISTERS], *argkinds[MAX_REGISTERS], *forallkinds[MAX_REGISTERS];
    int let_bodies[MAX_REGISTERS], let_nfvs[MAX_REGISTERS], let_fvs[MAX_REGISTERS][MAX_REGISTERS];
    struct gsparsedline *tyconts[MAX_CONTINUATIONS];
    int nregs, nglobals, ncodelabels, nfvs, nargs, nforalls, nlets, nconts;
    int i, j;
    struct gstype_expr *expr, *oexpr;
    struct gstype_expr_summary *res;
    void *dest;
    ulong size_fvs;
    enum {
        regglobal,
        regcode,
        regfv,
        regarg,
        regforall,
        reglet,
    } regclass;

    nregs = 0;
    nglobals = 0;
    ncodelabels = 0;
    nfvs = 0;
    nargs = 0;
    nforalls = 0;
    nlets = 0;
    nconts = 0;

    regclass = regglobal;
    for (; ; p = gsinput_next_line(p)) {
        if (gssymeq(p->directive, gssymtypeop, ".tygvar")) {
            if (nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
            if (regclass > regglobal)
                gsfatal_bad_input(p, "Too late to add type globals")
            ;
            regs[nregs] = p->label;
            tyglobals[nglobals] = gssymtable_get_type(symtable, p->label);
            if (!tyglobals[nregs])
                gsfatal_bad_input(p, "Couldn't find referent %s", p->label->name)
            ;
            nregs++, nglobals++;
        } else if (gssymeq(p->directive, gssymtypeop, ".tycode")) {
            if (ncodelabels >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Code register overflow")
            ;
            if (regclass > regcode)
                gsfatal_bad_input(p, "Too late to add code labels")
            ;
            regclass = regcode;
            codelables[ncodelabels] = p->label;
            codelabledests[ncodelabels] = gssymtable_get_type(symtable, p->label);
            if (!codelabledests[ncodelabels])
                gsfatal_bad_input(p, "Couldn't find referent %s", p->label->name)
            ;
            ncodelabels++;
        } else if (gssymeq(p->directive, gssymtypeop, ".tyfv")) {
            if (nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
            if (regclass > regfv)
                gsfatal_bad_input(p, "Too late to add free type variables")
            ;
            regclass = regfv;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "kind");
            fvkinds[nfvs] = gskind_compile(p, p->arguments[0]);
            nregs++, nfvs++;
        } else if (gssymeq(p->directive, gssymtypeop, ".tyarg")) {
            if (nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
            if (regclass > regarg)
                gsfatal_bad_input(p, "Too late to add type arguments")
            ;
            regclass = regarg;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "kind");
            argkinds[nargs] = gskind_compile(p, p->arguments[0]);
            nregs++, nargs++;
        } else if (gssymeq(p->directive, gssymtypeop, ".tyforall")) {
            if (nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
            if (regclass > regforall)
                gsfatal_bad_input(p, "Too late to add foralls")
            ;
            regclass = regforall;
            regs[nregs] = p->label;
            gsargcheck(p, 0, "kind");
            forallkinds[nforalls] = gskind_compile(p, p->arguments[0]);
            nregs++, nforalls++;
        } else if (gssymeq(p->directive, gssymtypeop, ".tylet")) {
            int body = 0;
            int fvs[MAX_REGISTERS];

            if (nregs >= MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Register overflow")
            ;
            if (regclass > reglet)
                gsfatal_bad_input(p, "Too late to add type lets")
            ;
            regclass = reglet;
            regs[nregs] = p->label;

            gsargcheck(p, 0, "code label");
            for (i = 0; i < ncodelabels; i++) {
                if (codelables[i] == p->arguments[0]) {
                    body = i;
                    goto have_let_body;
                }
            }
            gsfatal_bad_input(p, "Un-declared type expr code label %s", p->arguments[0]->name);
        have_let_body:
            for (i = 0; i < p->numarguments - 1; i++) {
                for (j = 0; j < nregs; j++) {
                    if (regs[j] == p->arguments[1 + i]) {
                        fvs[i] = j;
                        goto have_register_for_let;
                    }
                }
                gsfatal_bad_input(p, "Cannot find type register %s", p->arguments[1 + i]->name);
            have_register_for_let:
                ;
            }

            let_bodies[nlets] = body;
            let_nfvs[nlets] = p->numarguments - 1;
            for (i = 0; i < p->numarguments - 1; i++) {
                let_fvs[nlets][i] = fvs[i];
            }

            nregs++, nlets++;
        } else if (gssymeq(p->directive, gssymtypeop, ".typeapp")) {
            PUSH_CONTINUATION;
        } else if (gssymeq(p->directive, gssymtypeop, ".tylift")) {
            PUSH_CONTINUATION;
        } else if (gssymeq(p->directive, gssymtypeop, ".tyref")) {
            struct gstype_expr_ref *ref;

            ref = gs_sys_seg_suballoc(&gstype_expr_descr, &gstype_expr_nursury, sizeof(*ref), sizeof(int));

            ref->e.node = gstype_ref;
            ref->e.file = p->file;
            ref->e.lineno = p->lineno;
            gsargcheck(p, 0, "referent");
            for (i = 0; i < nregs; i++) {
                if (regs[i] == p->arguments[0]) {
                    ref->referent = i;
                    goto have_register_for_ref;
                }
            }
            gsfatal("%s:%d: Undeclared register %s", p->file->name, p->lineno, p->arguments[0]->name);
        have_register_for_ref:
            expr = (struct gstype_expr *)ref;
            goto have_expr;
        } else if (gssymeq(p->directive, gssymtypeop, ".tysum")) {
            struct gstype_expr_sum *sum;

            if (p->numarguments % 2)
                gsfatal_bad_input(p, "Cannot have odd number of arguments to .tysum");

            sum = gs_sys_seg_suballoc(&gstype_expr_descr, &gstype_expr_nursury, sizeof(*sum) + p->numarguments / 2 * sizeof(*sum->constrs), sizeof(int));

            sum->e.node = gstype_sum;
            sum->e.file = p->file;
            sum->e.lineno = p->lineno;

            sum->numconstrs = p->numarguments / 2;
            for (i = 0; i < p->numarguments; i += 2) {
                for (j = 0; j < nregs; j++) {
                    if (p->arguments[i + 1] == regs[j]) {
                        sum->constrs[i].name = p->arguments[i];
                        sum->constrs[i].arg = j;
                        goto have_register_for_constr_arg;
                    }
                }
                gsfatal_bad_input(p, "Undeclared register %s", p->arguments[i + 1]->name);
            have_register_for_constr_arg:
                ;
            }
            expr = (struct gstype_expr *)sum;
            goto have_expr;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Un-implemented type expression op %s", p->directive->name);
        }
    }

have_expr:

    while (nconts) {
        p = tyconts[--nconts];
        oexpr = expr;
        if (gssymeq(p->directive, gssymtypeop, ".tylift")) {
            struct gstype_expr_lift *lift;

            lift = gs_sys_seg_suballoc(&gstype_expr_descr, &gstype_expr_nursury, sizeof(*lift), sizeof(int));

            lift->e.node = gstype_lift;
            lift->e.file = p->file;
            lift->e.lineno = p->lineno;
            lift->arg = oexpr;

            expr = (struct gstype_expr *)lift;
        } else if (gssymeq(p->directive, gssymtypeop, ".typeapp")) {
            struct gstype_expr_app *app;
            int args[MAX_REGISTERS];

            if (p->numarguments > MAX_REGISTERS)
                gsfatal_unimpl_input(__FILE__, __LINE__, p, "Too many arguments to .typeapp; max 0x%x, but 0x%x supplied",
                    MAX_REGISTERS,
                    p->numarguments
                )
            ;
            for (i = 0; i < p->numarguments; i++) {
                for (j = 0; j < nregs; j++) {
                    if (regs[j] == p->arguments[i]) {
                        args[i] = j;
                        goto have_register_for_arg;
                    }
                }
                gsfatal("%s:%d: No such register %s", p->file->name, p->lineno, p->arguments[i]->name);
            have_register_for_arg:
                ;
            }

            app = gs_sys_seg_suballoc(&gstype_expr_descr, &gstype_expr_nursury, sizeof(*app) + p->numarguments * sizeof(app->args[0]), sizeof(int));

            app->e.node = gstype_app;
            app->e.file = p->file;
            app->e.lineno = p->lineno;
            app->numargs = p->numarguments;
            for (i = 0; i < p->numarguments; i++)
                app->args[i] = args[i];
            app->fun = oexpr;

            expr = (struct gstype_expr *)app;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "type expression continuation %s", p->directive->name);
        }
    }

    size_fvs = 0;
    for (i = 0; i < nlets; i++) {
        size_fvs += let_nfvs[i] * sizeof(*res->lets[0].fvs);
    }
    dest = gs_sys_seg_suballoc(
        &gstype_expr_summary_descr,
        &gstype_expr_summary_nursury,
        sizeof(*res)
            + nglobals * sizeof(*res->globals)
            + nglobals * sizeof(*res->global_vars)
            + ncodelabels * sizeof(*res->code_labels)
            + ncodelabels * sizeof(*res->code_label_dests)
            + nfvs * sizeof(*res->fvkinds)
            + nargs * sizeof(*res->argkinds)
            + nforalls * sizeof(*res->forallkinds)
            + nlets * sizeof(*res->lets)
            + size_fvs
        ,
        sizeof(int)
    );
    res = dest; dest = (uchar*)dest + sizeof(*res);
    res->numregs = nregs;
    res->numglobals = nglobals;
    res->globals = dest; dest = (uchar*)dest + nglobals * sizeof(*res->globals);
    res->global_vars = dest; dest = (uchar*)dest + nglobals * sizeof(*res->global_vars);
    for (i = 0; i < nglobals; i++) {
        res->globals[i] = tyglobals[i];
        res->global_vars[i] = regs[i];
    }
    res->numcodelabels = ncodelabels;
    res->code_labels = dest; dest = (uchar*)dest + ncodelabels * sizeof(*res->code_labels);
    res->code_label_dests = dest; dest = (uchar*)dest + ncodelabels * sizeof(*res->code_label_dests);
    for (i = 0; i < ncodelabels; i++) {
        res->code_labels[i] = codelables[i];
        res->code_label_dests[i] = codelabledests[i];
    }
    res->numfvs = nfvs;
    res->fvkinds = dest; dest = (uchar*)dest + nfvs * sizeof(*res->fvkinds);
    for (i = 0; i < nfvs; i++) {
        res->fvkinds[i] = fvkinds[i];
    }
    res->numargs = nargs;
    res->argkinds = dest; dest = (uchar*)dest + nargs * sizeof(*res->argkinds);
    for (i = 0; i < nargs; i++) {
        res->argkinds[i] = argkinds[i];
    }
    res->numforalls = nforalls;
    res->forallkinds = dest; dest = (uchar*)dest + nforalls * sizeof(*res->forallkinds);
    for (i = 0; i < nforalls; i++) {
        res->forallkinds[i] = forallkinds[i];
    }
    res->numlets = nlets;
    res->lets = dest; dest = (uchar*)dest + nlets * sizeof(*res->lets);
    for (i = 0; i < nlets; i++) {
        res->lets[i].numfvs = let_nfvs[i];
        res->lets[i].body = let_bodies[i];
        res->lets[i].fvs = dest; dest = (uchar*)dest + res->lets[i].numfvs * sizeof(*res->lets[i].fvs);
    }
    if (nregs > nglobals + nfvs + nargs + nforalls + nlets)
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "Registers after arguments next")
    ;
    res->code = expr;

    return res;
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
