/* §source.file{Byte-compiler} */

/* NB: We also do type-checking of gsac files in this file */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsbytecompile.h"
#include "gsheap.h"
#include "gstopsort.h"

void
gsbc_alloc_data_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, int n)
{
    uint total_size, offsets[MAX_ITEMS_PER_SCC];
    int i, data_size;
    void *base;

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    data_size = sizeof(struct gsbco*);

    total_size = 0;
    for (i = 0; i < n; i++) {
        int size;

        offsets[i] = total_size;
        size = items[i].type == gssymdatalable ? data_size : 0;
        total_size += size;
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gsreserveheap(total_size);

    for (i = 0; i < n; i++) {
        if (items[i].type == gssymdatalable) {
            heap[i] = (gsvalue)((uchar*)base + offsets[i]);
            gssymtable_set_data(symtable, items[i].v.pdata->label, heap[i]);
        }
    }
}

static int gsbc_size_item(struct gsbc_item item);

void
gsbc_alloc_code_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gsbco **bcos, int n)
{
    uint total_size, offsets[MAX_ITEMS_PER_SCC];
    int i;
    void *base;

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    total_size = 0;
    for (i = 0; i < n; i++) {
        int size;

        offsets[i] = total_size;
        size = items[i].type == gssymcodelable ? gsbc_size_item(items[i]) : 0;
        total_size += size;
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gsreservebytecode(total_size);

    for (i = 0; i < n; i++) {
        if (items[i].type == gssymcodelable) {
            bcos[i] = (struct gsbco*)((uchar*)base + offsets[i]);
            gssymtable_set_code(symtable, items[i].v.pdata->label, bcos[i]);
        }
    }
}

static
int
gsbc_size_item(struct gsbc_item item)
{
    int size;
    struct gsparsedline *p;
    enum {
        phgvars,
        phbytecodes,
    } phase;

    size = sizeof(struct gsbco);

    phase = phgvars;
    for (p = gsinput_next_line(item.v.pcode); ; p = gsinput_next_line(p)) {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_size_item");
#if 0
    next_phase:
        switch (phase) {
            case phgvars:
                if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
                    if (size % sizeof(void*))
                        gsfatal("%s:%d: %s:%d: File format error: we're at a .gvar generator but our location isn't void*-aligned",
                            __FILE__,
                            __LINE__,
                            p->file->name,
                            p->lineno
                        )
                    ;
                    size += sizeof(void*);
                } else {
                    phase = phbytecodes;
                    goto next_phase;
                }
                break;
            case phbytecodes:
                if (gssymeq(p->directive, gssymcodeop, ".app")) {
                    size += 1; /* Byte code */
                    size += 1; /* n */
                    if (p->numarguments >= 0x100)
                        gsfatal("%s:%d: .app continuations may not have more than 0xff arguments", p->file->name, p->lineno);
                    size += p->numarguments;
                } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
                    size += 1; /* Byte code */
                    size += 1; /* Argument */
                    if (p->numarguments > 1)
                        gsfatal("%s:%d: Too many arguments to .enter", p->file->name, p->lineno);
                    goto done;
                } else {
                    gsfatal("%s:%d: %s:%d: Size '%s' generators next", __FILE__, __LINE__, p->file->name, p->lineno, p->directive->name);
                }
                break;
            default:
                gsfatal("%s:%d: Unknown phase %d", __FILE__, __LINE__, phase);
        }

done:
#endif
    }

    if (size % sizeof(void*))
        size += sizeof(void*) - size
    ;

    return size;
}

/* §section{Actual Byte Compiler} */

static void gsbc_bytecompile_data_item(struct gsparsedline *, gsvalue *, int, int);

void
gsbc_bytecompile_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, struct gsbco **bcos, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymtypelable:
                break;
            case gssymdatalable:
                gsbc_bytecompile_data_item(items[i].v.pdata, heap, i, n);
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v.pcode, "gsbc_bytecompile_scc(type = %d)", items[i].type);
        }
    }
}

static struct gsbco *gsbc_undefined(void);

static
void
gsbc_bytecompile_data_item(struct gsparsedline *p, gsvalue *heap, int i, int n)
{
    struct gsbco **hp;

    hp = (struct gsbco **)heap[i];

    if (gssymeq(p->directive, gssymdatadirective, ".undefined")) {
        *hp = gsbc_undefined();
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "Data directive %s next", p->directive->name);
    }
}

static struct gsbco *gsbc_undefined_constant;

static
struct gsbco *
gsbc_undefined()
{
    if (!gsbc_undefined_constant) {
        gsbc_undefined_constant = gsreservebytecode(sizeof(struct gsbco));
        gsbc_undefined_constant->tag = gsbco_undefined;
        gsbc_undefined_constant->size = sizeof(struct gsbco);
    }

    return gsbc_undefined_constant;
}

/* TODO: REMOVE BELOW THIS OR MOVE ABOVE THIS COMMENT */

#if 0
static void gsbc_typecheck(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, struct gsbc_item *items, int, struct gsbc_item item);
#endif

/* §subsection{Type-checker} */

#if 0
static struct gsbc_code_item_type *gsbc_typecheck_code_expr(struct gsfile_symtable *, struct gsparsedline *);
static struct gsbc_type *gsbc_typecheck_compile_type_expr(struct gsfile_symtable *, struct gsparsedline *);
static struct gsbc_type_item_kind *gsbc_typecheck_kind_type(struct gsfile_symtable *, struct gsbc_type *);
static struct gsbc_type *gsbc_typecheck_compile_prim_type(struct gsfile_symtable *, struct gsparsedline *);
static struct gsbc_type_item_kind *gsbc_typecheck_check_prim_kind(struct gsfile_symtable *, struct gsbc_type *);
static struct gsbc_type *gsbc_typecheck_compile_abstract_type(struct gsfile_symtable *, struct gsparsedline *);
static struct gsbc_type_item_kind *gsbc_typecheck_check_abstract_kind(struct gsfile_symtable *, struct gsbc_type *);

static
void
gsbc_typecheck(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, struct gsbc_item *items, int nitems, struct gsbc_item item)
{
    switch (item.type) {
        case gssymcodelable:
            if (gssymeq(item.v.pcode->directive, gssymcodedirective, ".expr")) {
                struct gsbc_code_item_type *type;

                type = gsbc_typecheck_code_expr(symtable, gsinput_next_line(item.v.pcode));
                if (item.v.pcode->label)
                    gssymtable_set_expr_type(symtable, item.v.pcode->label, type);
                ;
            } else {
                gsfatal("%s:%d: %s:%d: Cannot type check code items of type '%s' yet",
                    __FILE__,
                    __LINE__,
                    item.v.pcode->file->name,
                    item.v.pcode->lineno,
                    item.v.pcode->directive->name
                );
            }
            return;
    }
}

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

static struct gsbc_type *gsbc_type_alloc();

static struct gsbc_type_expr_summary *gsbc_typecheck_compile_type_ops(struct gsfile_symtable *, struct gsparsedline *);

static
struct gsbc_type *
gsbc_typecheck_compile_type_expr(struct gsfile_symtable *symtable, struct gsparsedline *p)
{
    struct gsbc_type *res;

    res = gsbc_type_alloc();
    res->node = gsbc_type_expr;
    res->a.expr = gsbc_typecheck_compile_type_ops(symtable, p);

    return res;
}
#endif

#define TYPE_CHECKER_STACK_SIZE 0x100;

#if 0
static
struct gsbc_type_item_kind *
gsbc_typecheck_kind_type(struct gsfile_symtable *symtable, struct gsbc_type *type)
{
/*    struct gsbc_type_item_kind *res;*/

    switch (type->node) {
/*
            case gsbc_type_prim: {
                kind = gsbc_type_item_kind_alloc(0);
                res->kind = type->a.prim.kind;
                return res;
            }
*/
        case gsbc_type_expr:
            return gsbc_typecheck_kind_type_expr_summary(symtable, type->a.expr);
        default:
            gsfatal("%s:%d: gsbc_typecheck_kind_type(node = %d)", __FILE__, __LINE__, type->node);
    }

    gsfatal("%s:%d: gsbc_typecheck_kind_type next", __FILE__, __LINE__);

    return 0;
}

static
struct gsbc_type *
gsbc_typecheck_compile_prim_type(struct gsfile_symtable *symtable, struct gsparsedline *ptype)
{
    struct gsbc_type *res;

    if (ptype->numarguments < 3)
        gsfatal("%s:%d: Missing primset, name or kind on primtype", ptype->file->name, ptype->lineno);

    res = gsbc_type_alloc(0);
    res->node = gsbc_type_prim;
    res->file = ptype->file;
    res->lineno = ptype->lineno;
    res->a.prim.primset = gsprims_lookup_prim_set(ptype->arguments[0]->name);
    if (!res->a.prim.primset)
        gswarning("%s:%d: Warning: Unknown prim set %s", ptype->file->name, ptype->lineno, ptype->arguments[0]->name);
    res->a.prim.name = ptype->arguments[1];
    if (gssymeq(ptype->arguments[2], gssymkindexpr, "u"))
        res->a.prim.kind = gsbc_unlifted_kind();
    else
        res->a.prim.kind = gssymtable_get_kind(symtable, ptype->arguments[2]);
    if (!res->a.prim.kind)
        gsfatal("%s:%d: Unknown kind %s", ptype->file->name, ptype->lineno, ptype->arguments[2]->name);

    return res;
}

static struct gsbc_kind *gsbc_typecheck_compile_prim_kind(struct gsregistered_primkind *);

static
struct gsbc_kind *
gsbc_typecheck_compile_prim_kind(struct gsregistered_primkind *prim)
{
    switch (prim->node) {
        case gsprim_kind_unlifted:
            return gsbc_unlifted_kind();
        default:
            gsfatal("%s:%d: gsbc_typecheck_compile_prim_type_kind(%d)", __FILE__, __LINE__, prim->node);
    }

    return 0;
}

static
struct gsbc_type *
gsbc_typecheck_compile_abstract_type(struct gsfile_symtable *symtable, struct gsparsedline *type)
{
    struct gsbc_type *res;

    if (type->numarguments < 1)
        gsfatal("%s:%d: Missing kind on abstype", type->file->name, type->lineno);

    res = gsbc_type_alloc(0);
    res->node = gsbc_type_abstract;
    res->file = type->file;
    res->lineno = type->lineno;
    res->a.expr = gsbc_typecheck_compile_type_ops(symtable, gsinput_next_line(type));

    gsfatal("%s:%d: gsbc_typecheck_compile_abstract_type next", __FILE__, __LINE__);
    return 0;
}

/* §subsection{Sizing items} */

#endif

/* §section{Topological Sort (to decide order to compile file in)} */

#define GSBC_ITEM_STACK_CAPACITY 0x100

struct gsbc_item_stack {
    ulong size;
    struct gsbc_item items[GSBC_ITEM_STACK_CAPACITY];
};

union gsbc_item_hash_value {
    ulong ul;
};

struct gsbc_item_hash_link {
    struct gsbc_item_hash_link *next;
    struct gsbc_item key;
    union gsbc_item_hash_value value;
};

struct gsbc_item_hash {
    ulong size;
    ulong nbuckets;
    struct gsbc_item_hash_link *buckets[];
};

static struct gsbc_item_hash *gsbc_alloc_item_hash(void);

static void gsbc_topsort_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_stack_initialize(struct gsbc_item_stack *);

struct gsbc_scc *
gsbc_topsortfile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable)
{
    struct gsbc_scc *res, **pend;
    ulong c;
    struct gsbc_item_stack unassigned_items, maybe_group_items;
    struct gsbc_item_hash *preorders;

    c = 1;
    res = 0;
    pend = &res;
    preorders = gsbc_alloc_item_hash();
    gsbc_stack_initialize(&unassigned_items);
    gsbc_stack_initialize(&maybe_group_items);

    switch (parsedfile->type) {
        case gsfileprefix: {
            struct gsbc_item item;
            int i;
            item.file = parsedfile;
            if (!parsedfile->data && !parsedfile->types)
                gsfatal("%s: Cannot compile file: no data section so no entry points", parsedfile->name->name);
            item.type = gssymdatalable;
            item.v.pdata = 0;
            if (parsedfile->data) for (i = 0; i < parsedfile->data->numitems; i++) {
                if (item.v.pdata)
                    gsfatal("%s: Topologically sort from second data item next", parsedfile->name->name);
                item.v.pdata = GSDATA_SECTION_FIRST_ITEM(parsedfile->data);
                if (gssymtable_get_scc(symtable, item))
                    continue;
                gsbc_topsort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            }
            item.type = gssymtypelable;
            item.v.ptype = 0;
            if (parsedfile->types) for (i = 0; i < parsedfile->types->numitems; i++) {
                if (item.v.ptype)
                    item.v.ptype = gstype_section_next_item(item.v.ptype)
                ; else
                    item.v.ptype = GSTYPE_SECTION_FIRST_ITEM(parsedfile->types)
                ;
                if (gssymtable_get_scc(symtable, item))
                    continue;
                gsbc_topsort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            }
            return res;
        }
        case gsfiledocument: {
            struct gsbc_item item;
            item.file = parsedfile;
            item.type = gssymdatalable;
            item.v.pdata = GSDATA_SECTION_FIRST_ITEM(parsedfile->data);
            if (!parsedfile->data)
                gsfatal("%s: Cannot compile file: no data section so no entry points", parsedfile->name->name);
            if (!parsedfile->data->numitems)
                gsfatal("%s: Document lacks first data item; cannot find entry point", parsedfile->name->name);
            gsbc_topsort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            return res;
        }
        default:
            gsfatal("gsbc_topsortfile: %s: unknown file type %d", parsedfile->name->name, parsedfile->type);
    }

    gsfatal("gsbc_topsortfile(%s) next", parsedfile->name->name);
    return res;
}

static struct gsparsedline *gstype_section_skip_type_expr(struct gsparsedline *);

struct gsparsedline *
gstype_section_next_item(struct gsparsedline *type)
{
    if (gssymeq(type->directive, gssymtypedirective, ".tyexpr")) {
        return gstype_section_skip_type_expr(gsinput_next_line(type));
    } else if (gssymeq(type->directive, gssymtypedirective, ".tyabstract")) {
        return gstype_section_skip_type_expr(gsinput_next_line(type));
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, type, "gstype_section_next_item(%s)", type->directive->name);
    }
    return 0;
}

static struct gsparsedline *
gstype_section_skip_type_expr(struct gsparsedline *p)
{
    for (;;) {
        if (
            gssymeq(p->directive, gssymtypeop, ".tygvar")
            || gssymeq(p->directive, gssymtypeop, ".tycode")
            || gssymeq(p->directive, gssymtypeop, ".tyarg")
            || gssymeq(p->directive, gssymtypeop, ".tyfv")
            || gssymeq(p->directive, gssymtypeop, ".tyforall")
            || gssymeq(p->directive, gssymtypeop, ".tylift")
            || gssymeq(p->directive, gssymtypeop, ".tylet")
            || gssymeq(p->directive, gssymtypeop, ".typeapp")
        )
            p = gsinput_next_line(p);
        else if (
            gssymeq(p->directive, gssymtypeop, ".tyref")
            || gssymeq(p->directive, gssymtypeop, ".tysum")
        )
            return gsinput_next_line(p);
        else
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gstype_section_skip_type_expr(%s)", p->directive->name);
    }

    return 0;
}

static
void
gsbc_stack_initialize(struct gsbc_item_stack *pstack)
{
    pstack->size = 0;
}

static ulong gsbc_preorder_get(struct gsbc_item_hash *preorders, struct gsbc_item item);
static void gsbc_preorder_update(struct gsbc_item_hash *, struct gsbc_item, ulong);
static void gsbc_push(struct gsbc_item_stack *, struct gsbc_item);
static int gsbc_stack_in(struct gsbc_item_stack *, struct gsbc_item);
static struct gsbc_item gsbc_top(struct gsbc_item_stack *);
static struct gsbc_item gsbc_pop(struct gsbc_item_stack *);
static struct gsbc_scc *gsbc_alloc_scc(struct gsbc_item);
static void gsbc_top_sort_subitems_of_data_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_top_sort_subitems_of_code_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_top_sort_subitems_of_type_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

/* Based on §url{http://en.wikipedia.org/wiki/Path-based_strong_component_algorithm} */

static
void
gsbc_topsort_outgoing_edge(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    ulong n;

    if (gssymtable_get_scc(symtable, item))
        return;
    if (n = gsbc_preorder_get(preorders, item)) {
        if (gsbc_stack_in(unassigned_items, item))
            while (gsbc_preorder_get(preorders, gsbc_top(maybe_group_items)) > n)
                gsbc_pop(maybe_group_items)
        ;
    } else {
        gsbc_topsort_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
    }
}

static
void
gsbc_topsort_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    gsbc_preorder_update(preorders, item, (*pc)++);
    gsbc_push(unassigned_items, item);
    gsbc_push(maybe_group_items, item);
    switch (item.type) {
        case gssymdatalable:
            gsbc_top_sort_subitems_of_data_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
            break;
        case gssymcodelable:
            gsbc_top_sort_subitems_of_code_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
            break;
        case gssymtypelable:
            gsbc_top_sort_subitems_of_type_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
            break;
        default:
            gsfatal("%s:%d: %s: gsbc_subtop_sort(type = %d) next", __FILE__, __LINE__, item.file->name->name, item.type);
    }
    if (
        maybe_group_items->size > 0
        && gsbc_item_eq(gsbc_top(maybe_group_items), item)
    ) {
        struct gsbc_scc *pnew, *p, **plast;
        struct gsbc_item last;
        plast = &pnew;
        gsbc_item_empty(&last);
        do {
            last = gsbc_pop(unassigned_items);
            *plast = gsbc_alloc_scc(last);
            plast = &(*plast)->next_item;
        } while (!gsbc_item_eq(last, item));
        for (p = pnew; p; p = p->next_item)
            gssymtable_set_scc(symtable, p->item, pnew);
        **pend = pnew;
        *pend = &pnew->next_scc;
        gsbc_pop(maybe_group_items);
    }
}

static
void
gsbc_top_sort_subitems_of_data_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    gsinterned_string directive = item.v.pdata->directive;

    if (gssymeq(directive, gssymdatadirective, ".closure")) {
        struct gsbc_item code, type;
        code = gssymtable_lookup(
            item.v.pdata->file->name,
            item.v.pdata->lineno,
            symtable,
            item.v.pdata->arguments[0]
        );
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, code, pend, pc);
        if (item.v.pdata->numarguments >= 2) {
            type = gssymtable_lookup(
                item.v.pdata->file->name,
                item.v.pdata->lineno,
                symtable,
                item.v.pdata->arguments[1]
            );
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, type, pend, pc);
        }
        if (item.v.pdata->numarguments > 2)
            gsfatal("%s:%d: Panic: data item %s:%s has more than two arguments; I don't know what to do!",
                __FILE__,
                __LINE__,
                item.file->name->name,
                item.v.pdata->label->name
            )
        ;
    } else if (gssymeq(directive, gssymdatadirective, ".tyapp")) {
        struct gsbc_item fn, tyarg;
        int i;

        fn = gssymtable_lookup(
            item.v.pdata->file->name,
            item.v.pdata->lineno,
            symtable,
            item.v.pdata->arguments[0]
        );
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, fn, pend, pc);

        for (i = 1; i < item.v.pdata->numarguments; i++) {
            tyarg = gssymtable_lookup(
                item.v.pdata->file->name,
                item.v.pdata->lineno,
                symtable,
                item.v.pdata->arguments[1]
            );
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, tyarg, pend, pc);
        }
    } else if (gssymeq(directive, gssymdatadirective, ".undefined")) {
        struct gsbc_item ty;

        ty = gssymtable_lookup(
            item.v.pdata->file->name,
            item.v.pdata->lineno,
            symtable,
            item.v.pdata->arguments[0]
        );
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, ty, pend, pc);
    } else {
        gsfatal("%s:%d: %s:%d: gsbc_subtop_sort(data item; directive = %s) next", __FILE__, __LINE__, item.v.pdata->file->name, item.v.pdata->lineno, item.v.pdata->directive->name);
    }
}

enum gsbc_code_directive {
    gscode_directive_expr,
    gsnum_code_directives,
};

static enum gsbc_code_directive gsbc_lookup_code_directive(gsinterned_string);

enum gsbc_code_op {
    gscode_op_gvar,
    gscode_op_app,
    gsnum_code_ops,
};

static enum gsbc_code_op gsbc_lookup_opcode(gsinterned_string);

static
void
gsbc_top_sort_subitems_of_code_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    enum gsbc_code_directive directive;
    enum gsbc_code_op opcode;
    struct gsparsedline *p;

    directive = gsbc_lookup_code_directive(item.v.pcode->directive);
    switch (directive) {
        case gscode_directive_expr:
            for (p = gsinput_next_line(item.v.pcode); ; p = gsinput_next_line(p)) {
                opcode = gsbc_lookup_opcode(p->directive);
                switch (opcode) {
                    case gscode_op_gvar:
                        {
                            struct gsbc_item global;
                            global = gssymtable_lookup(p->file->name, p->lineno, symtable, p->label);
                            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
                        }
                        break;
                    default:
                        return;
                }
            }
            break;
        default:
            gsfatal("%s:%d: %s:%d: gsbc_subtop_sort(directive = %s) next", __FILE__, __LINE__, item.v.pcode->file->name, item.v.pcode->lineno, item.v.pcode->directive->name);
    }

    gsfatal("%s:%d: gsbc_subtop_sort_code_item next", __FILE__, __LINE__);
}

struct {
    char *name;
    gsinterned_string interned;
} gsbc_code_directive_table[] = {
    /* gscode_directive_expr = */ { ".expr", 0, },
    { 0, 0, },
};

static
enum gsbc_code_directive
gsbc_lookup_code_directive(gsinterned_string dir)
{
    enum gsbc_code_directive i;

    for (i = 0; i < gsnum_code_directives; i++) {
        if (!gsbc_code_directive_table[i].name)
            gsfatal("Missing items in gsbc_code_directive_table; only %d items", i);
        if (!gsbc_code_directive_table[i].interned)
            gsbc_code_directive_table[i].interned = gsintern_string(gssymcodedirective, gsbc_code_directive_table[i].name);
        if (gsbc_code_directive_table[i].interned == dir)
            return i;
    }

    gsfatal("%s: Unknown code directive", dir->name);

    return -1;
}

struct {
    char *name;
    gsinterned_string interned;
} gsbc_code_op_table[] = {
    /* gscode_op_gvar = */ { ".gvar", 0, },
    /* gscode_op_app = */ { ".app", 0, },
    { 0, 0, },
};

static
enum gsbc_code_op
gsbc_lookup_opcode(gsinterned_string dir)
{
    enum gsbc_code_op i;

    for (i = 0; i < gsnum_code_ops; i++) {
        if (!gsbc_code_op_table[i].name)
            gsfatal("Missing items in gsbc_code_op_table; only %d items", i);
        if (!gsbc_code_op_table[i].interned)
            gsbc_code_op_table[i].interned = gsintern_string(gssymcodeop, gsbc_code_op_table[i].name);
        if (gsbc_code_op_table[i].interned == dir)
            return i;
    }

    gsfatal("%s:%d: Unknown code op '%s'", __FILE__, __LINE__, dir->name);

    return -1;
}

static void gsbc_top_sort_subitems_of_type_expr(struct gsfile_symtable *, struct gsbc_item_hash *, struct gsbc_item_stack *, struct gsbc_item_stack *, struct gsbc_item, struct gsbc_scc ***, ulong *, struct gsparsedline *);

static
void
gsbc_top_sort_subitems_of_type_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    gsinterned_string directive;
    struct gsparsedline *ptype;

    ptype = item.v.ptype;
    directive = ptype->directive;
    if (gssymeq(directive, gssymtypedirective, ".tyexpr")) {
        gsbc_top_sort_subitems_of_type_expr(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc, gsinput_next_line(ptype));
        return;
    } else if (gssymeq(directive, gssymtypedirective, ".tyabstract")) {
        gsbc_top_sort_subitems_of_type_expr(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc, gsinput_next_line(ptype));
        return;
    } else if (gssymeq(directive, gssymtypedirective, ".tydefinedprim")) {
        struct gsregistered_primset *prims;
        struct gsregistered_primtype *type;

        if (ptype->numarguments < 1)
            gsfatal("%s:%d: Missing primitive set name on .tydefinedprim", ptype->file->name, ptype->lineno);
        if (!(prims = gsprims_lookup_prim_set(ptype->arguments[0]->name)))
            gsfatal("%s:%d: Unknown primitive set %s, which is really bad since it's supposed to contain defined types",
                ptype->file->name,
                ptype->lineno,
                ptype->arguments[0]->name
            )
        ;
        if (ptype->numarguments < 2)
            gsfatal("%s:%d: Missing primitive type name on .tydefinedprim", ptype->file->name, ptype->lineno);
        if (!(type = gsprims_lookup_type(prims, ptype->arguments[1]->name)))
            gsfatal("%s:%d: Primitive set %s does not in fact contain a type %s", ptype->file->name, ptype->lineno, ptype->arguments[0]->name, ptype->arguments[1]->name);
        if (type->prim_type_group != gsprim_type_defined)
            gsfatal("%s:%d: Primite type %s in primitive set %s is not in fact a defined type", ptype->file->name, ptype->lineno, ptype->arguments[1]->name, ptype->arguments[0]->name);
        if (ptype->numarguments < 3)
            gsfatal("%s:%d: Missing kind on .tydefinedprim", ptype->file->name, ptype->lineno);
        if (ptype->numarguments > 3)
            gsfatal("%s:%d: Too many arguments to .tydefinedprim", ptype->file->name, ptype->lineno);
        return;
    } else {
        gsfatal("%s:%d: %s:%d: gsbc_subtop_sort(directive = %s) next", __FILE__, __LINE__, ptype->file->name, ptype->lineno, ptype->directive->name);
    }

    gsfatal("%s:%d: %s:%d: gsbc_subtop_sort_type_item next", __FILE__, __LINE__, ptype->file->name, ptype->lineno);
}

static
void
gsbc_top_sort_subitems_of_type_expr(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc, struct gsparsedline *p)
{
    for (; ; p = gsinput_next_line(p)) {
        if (gssymeq(p->directive, gssymtypeop, ".tygvar")) {
            struct gsbc_item global;
            global = gssymtable_lookup(p->file->name, p->lineno, symtable, p->label);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
        } else if (gssymeq(p->directive, gssymtypeop, ".tycode")) {
            struct gsbc_item code;
            code = gssymtable_lookup(p->file->name, p->lineno, symtable, p->label);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, code, pend, pc);
        } else {
            return;
        }
    }
}

static int gsbc_item_hash_lookup(struct gsbc_item_hash *, struct gsbc_item, union gsbc_item_hash_value*);

static
ulong
gsbc_preorder_get(struct gsbc_item_hash *preorders, struct gsbc_item item)
{
    union gsbc_item_hash_value v;

    if (gsbc_item_hash_lookup(preorders, item, &v))
        return v.ul;
    else
        return 0;
}

static
int
gsbc_item_hash_lookup(struct gsbc_item_hash *phash, struct gsbc_item item, union gsbc_item_hash_value *pres)
{
    ulong hash_value;
    struct gsbc_item_hash_link *p;

    if (!phash)
        gsfatal("Forgot to allocate hash");

    hash_value = (uintptr)item.file->name;
    hash_value = hash_value * 33 + (uintptr)item.type;

    for (p = phash->buckets[hash_value % phash->nbuckets]; p; p = p->next) {
        if (gsbc_item_eq(p->key, item)) {
            *pres = p->value;
            return 1;
        }
    }

    return 0;
}

static void gsbc_item_hash_store(struct gsbc_item_hash *, struct gsbc_item, union gsbc_item_hash_value);

static
void
gsbc_preorder_update(struct gsbc_item_hash *preorders, struct gsbc_item item, ulong c)
{
    union gsbc_item_hash_value v;

    v.ul = c;

    gsbc_item_hash_store(preorders, item, v);
}

static struct gs_block_class gsbc_hash_link_segment = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte-compiler hash links",
};

static void *gsbc_hash_link_nursury;

static
void
gsbc_item_hash_store(struct gsbc_item_hash *phash, struct gsbc_item item, union gsbc_item_hash_value v)
{
    ulong hash_value;
    struct gsbc_item_hash_link *p, **plastp;

    if (!phash)
        gsfatal("Forgot to allocate hash");

    hash_value = (uintptr)item.file->name;
    hash_value = hash_value * 33 + (uintptr)item.type;

    plastp = &phash->buckets[hash_value % phash->nbuckets];
    for (p = phash->buckets[hash_value % phash->nbuckets]; p; p = p->next) {
        if (gsbc_item_eq(p->key, item)) {
            p->value = v;
            return;
        }
        plastp = &p->next;
    }

    *plastp = p = gs_sys_seg_suballoc(
        &gsbc_hash_link_segment,
        &gsbc_hash_link_nursury,
        sizeof(struct gsbc_item_hash_link),
        sizeof(struct gsbc_item_hash_link*)
    );

    p->next = 0;
    p->key = item;
    p->value = v;
}

static
void
gsbc_push(struct gsbc_item_stack *pstack, struct gsbc_item item)
{
    if (pstack->size >= GSBC_ITEM_STACK_CAPACITY)
        gsfatal("%s:%d: Expand stack next", __FILE__, __LINE__)
    ;

    pstack->items[pstack->size++] = item;
}

static
struct gsbc_item
gsbc_top(struct gsbc_item_stack *pstack)
{
    if (pstack->size < 1)
        gsfatal("Checking top of empty stack");

    return pstack->items[pstack->size - 1];
}

static
int
gsbc_stack_in(struct gsbc_item_stack *pstack, struct gsbc_item item)
{
    int i;

    for (i = 0; i < pstack->size; i++)
        if (gsbc_item_eq(pstack->items[i], item))
            return 1
    ;

    return 0;
}

static
struct gsbc_item
gsbc_pop(struct gsbc_item_stack *pstack)
{
    if (pstack->size < 1)
        gsfatal("Popping item off empty stack");

    return pstack->items[--pstack->size];
}

static struct gs_block_class gsbc_scc_segment = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte-compiler SCCs",
};

static void *gsbc_scc_nursury;

static
struct gsbc_scc *
gsbc_alloc_scc(struct gsbc_item item)
{
    struct gsbc_scc *res;

    res = gs_sys_seg_suballoc(&gsbc_scc_segment, &gsbc_scc_nursury, sizeof(*res), sizeof(void*));

    res->item = item;
    res->next_item = 0;
    res->next_scc = 0;

    return res;
}

int
gsbc_item_eq(struct gsbc_item item0, struct gsbc_item item1)
{
    if (item0.file != item1.file) return 0;
    if (item0.type != item1.type) return 0;
    switch (item0.type) {
        case gssymdatalable:
            if (item0.v.pdata != item1.v.pdata) return 0;
            break;
        case gssymcodelable:
            if (item0.v.pcode != item1.v.pcode) return 0;
            break;
        case gssymtypelable:
            if (item0.v.ptype != item1.v.ptype) return 0;
            break;
        default:
            gsfatal("%s:%d: gsbc_item_eq(type = %d)", __FILE__, __LINE__, item0.type);
    }

    return 1;
}

void
gsbc_item_empty(struct gsbc_item *pitem)
{
    pitem->file = 0;
    pitem->type = 0;
    pitem->v.pdata = 0;
}

struct gs_block_class gsbc_item_hash_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte compile item-keyed hashes",
};

void *gsbc_item_hash_nursury;

#define GSBC_INITIAL_BUCKET_COUNT 32

static
struct gsbc_item_hash *
gsbc_alloc_item_hash()
{
    struct gsbc_item_hash *res;
    ulong alloc_size;
    struct gsbc_item_hash_link **p;

    alloc_size = sizeof(*res) + GSBC_INITIAL_BUCKET_COUNT * sizeof(res->buckets[0]);

    res = gs_sys_seg_suballoc(&gsbc_item_hash_descr, &gsbc_item_hash_nursury, alloc_size, sizeof(ulong));

    res->size = 0;
    res->nbuckets = GSBC_INITIAL_BUCKET_COUNT;
    for (p = res->buckets; p < res->buckets + res->nbuckets; p++)
        *p = 0;

    return res;
}
