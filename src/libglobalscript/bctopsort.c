/* §source.file{Topological Sort (of String Code Files)} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gstopsort.h"

/* Based on §url{http://en.wikipedia.org/wiki/Path-based_strong_component_algorithm} */

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
            if (!parsedfile->data && !parsedfile->types && !parsedfile->coercions)
                gswarning("%s: Prefix file is empty", parsedfile->name->name);
            item.type = gssymdatalable;
            item.v = 0;
            if (parsedfile->data) for (i = 0; i < parsedfile->data->numitems; i++) {
                if (item.v)
                    item.v = gsinput_next_line(&item.pseg, item.v)
                ; else {
                    item.pseg = parsedfile->data->first_seg;
                    item.v = GSDATA_SECTION_FIRST_ITEM(parsedfile->data);
                }
                if (gssymtable_get_scc(symtable, item))
                    continue;
                gsbc_topsort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            }
            item.type = gssymtypelable;
            item.v = 0;
            if (parsedfile->types) for (i = 0; i < parsedfile->types->numitems; i++) {
                if (item.v) {
                    item.v = gstype_section_next_item(&item.pseg, item.v);
                } else {
                    item.pseg = parsedfile->types->first_seg;
                    item.v = GSTYPE_SECTION_FIRST_ITEM(parsedfile->types);
                }
                if (gssymtable_get_scc(symtable, item))
                    continue;
                gsbc_topsort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            }
            item.type = gssymcoercionlable;
            item.v = 0;
            if (parsedfile->coercions) for (i = 0; i < parsedfile->coercions->numitems; i++) {
                if (item.v) {
                    item.v = gscoercion_section_next_item(&item.pseg, item.v);
                } else {
                    item.pseg = parsedfile->coercions->first_seg;
                    item.v = GSCOERCION_SECTION_FIRST_ITEM(parsedfile->coercions);
                }
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
            item.v = GSDATA_SECTION_FIRST_ITEM(parsedfile->data);
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

static struct gsparsedline *gstype_section_skip_type_expr(struct gsparsedfile_segment **, struct gsparsedline *);

struct gsparsedline *
gstype_section_next_item(struct gsparsedfile_segment **ppseg, struct gsparsedline *type)
{
    static gsinterned_string gssymtyexpr, gssymtyabstract, gssymtyimpprim, gssymtyintrprim, gssymtyelimprim, gssymtydefinedprim;

    if (gssymceq(type->directive, gssymtyexpr, gssymtypedirective, ".tyexpr")) {
        return gstype_section_skip_type_expr(ppseg, gsinput_next_line(ppseg, type));
    } else if (gssymceq(type->directive, gssymtyabstract, gssymtypedirective, ".tyabstract")) {
        return gstype_section_skip_type_expr(ppseg, gsinput_next_line(ppseg, type));
    } else if (gssymceq(type->directive, gssymtyimpprim, gssymtypedirective, ".tyimpprim")) {
        return gsinput_next_line(ppseg, type);
    } else if (gssymceq(type->directive, gssymtyintrprim, gssymtypedirective, ".tyintrprim")) {
        return gsinput_next_line(ppseg, type);
    } else if (gssymceq(type->directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")) {
        return gsinput_next_line(ppseg, type);
    } else if (gssymceq(type->directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")) {
        return gsinput_next_line(ppseg, type);
    } else {
        gsfatal(UNIMPL("%P: gstype_section_next_item(%s)"), type->pos, type->directive->name);
    }
    return 0;
}

static
struct gsparsedline *
gstype_section_skip_type_expr(struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    static gsinterned_string gssymtygvar, gssymtyextabstype, gssymtylambda, gssymtyforall, gssymtyexists, gssymtylift, gssymtylet, gssymtyfun, gssymtyref, gssymtysum, gssymoptyubsum, gssymoptyproduct, gssymoptyubproduct;

    for (;;) {
        if (
            gssymceq(p->directive, gssymtygvar, gssymtypeop, ".tygvar")
            || gssymceq(p->directive, gssymtyextabstype, gssymtypeop, ".tyextabstype")
            || gssymceq(p->directive, gssymtylambda, gssymtypeop, ".tylambda")
            || gssymceq(p->directive, gssymtyforall, gssymtypeop, ".tyforall")
            || gssymceq(p->directive, gssymtyexists, gssymtypeop, ".tyexists")
            || gssymceq(p->directive, gssymtylift, gssymtypeop, ".tylift")
            || gssymceq(p->directive, gssymtylet, gssymtypeop, ".tylet")
            || gssymceq(p->directive, gssymtyfun, gssymtypeop, ".tyfun")
        )
            p = gsinput_next_line(ppseg, p);
        else if (
            gssymceq(p->directive, gssymtyref, gssymtypeop, ".tyref")
            || gssymceq(p->directive, gssymtysum, gssymtypeop, ".tysum")
            || gssymceq(p->directive, gssymoptyubsum, gssymtypeop, ".tyubsum")
            || gssymceq(p->directive, gssymoptyproduct, gssymtypeop, ".typroduct")
            || gssymceq(p->directive, gssymoptyubproduct, gssymtypeop, ".tyubproduct")
        )
            return gsinput_next_line(ppseg, p);
        else
            gsfatal(UNIMPL("%P: gstype_section_skip_type_expr(%y)"), p->pos, p->directive);
    }

    return 0;
}

static struct gsparsedline *gscoercion_section_skip_coercion_expr(struct gsparsedfile_segment **, struct gsparsedline *);

struct gsparsedline *
gscoercion_section_next_item(struct gsparsedfile_segment **ppseg, struct gsparsedline *coercion)
{
    static gsinterned_string gssymtycoercion;

    if (gssymceq(coercion->directive, gssymtycoercion, gssymcoerciondirective, ".tycoercion")) {
        return gscoercion_section_skip_coercion_expr(ppseg, gsinput_next_line(ppseg, coercion));
    } else {
        gsfatal(UNIMPL("%P: gscoercion_section_next_item(%y)"), coercion->pos, coercion->directive);
    }
    return 0;
}

static
struct gsparsedline *
gscoercion_section_skip_coercion_expr(struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    static gsinterned_string gssymtygvar, gssymtyextabstype, gssymtylambda, gssymtyinvert;
    static gsinterned_string gssymtydefinition;

    for (;;) {
        if (
            gssymceq(p->directive, gssymtygvar, gssymcoercionop, ".tygvar")
            || gssymceq(p->directive, gssymtyextabstype, gssymcoercionop, ".tyextabstype")
            || gssymceq(p->directive, gssymtylambda, gssymcoercionop, ".tylambda")
            || gssymceq(p->directive, gssymtyinvert, gssymcoercionop, ".tyinvert")
        )
            p = gsinput_next_line(ppseg, p);
        else if (
            gssymceq(p->directive, gssymtydefinition, gssymcoercionop, ".tydefinition")
        )
            return gsinput_next_line(ppseg, p);
        else
            gsfatal(UNIMPL("%P: gscoercion_section_skip_coercion_expr(%y)"), p->pos, p->directive);
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

static void gsbc_top_sort_subitems_of_coercion_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

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
        case gssymcoercionlable:
            gsbc_top_sort_subitems_of_coercion_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
            break;
        default:
            gsfatal(UNIMPL("%y: gsbc_subtop_sort(type = %d)"), item.file->name, item.type);
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
    static gsinterned_string gssymclosure, gssymrecord, gssymconstr, gssymrune, gssymstring, gssymlist, gssymregex, gssymundefined, gssymcast;

    gsinterned_string directive = item.v->directive;

    if (gssymceq(directive, gssymclosure, gssymdatadirective, ".closure")) {
        struct gsbc_item code, type;
        code = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[0]);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, code, pend, pc);
        if (item.v->numarguments >= 2) {
            type = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[1]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, type, pend, pc);
        }
        if (item.v->numarguments > 2)
            gsfatal(UNIMPL("%P: Too many arguments to .closure"), item.v->pos)
        ;
    } else if (gssymceq(directive, gssymrecord, gssymdatadirective, ".record")) {
        struct gsbc_item fieldvalue, type;
        int i;

        for (i = 0; i < item.v->numarguments && item.v->arguments[i]->type != gssymseparator; i += 2) {
            fieldvalue = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[i + 1]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, fieldvalue, pend, pc);
        }
        if (i < item.v->numarguments) {
            i++;
            for (; i < item.v->numarguments; i++) {
                type = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[i]);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, type, pend, pc);
            }
        }
    } else if (gssymceq(directive, gssymconstr, gssymdatadirective, ".constr")) {
        struct gsbc_item type;
        int i;

        type = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[0]);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, type, pend, pc);

        if (item.v->numarguments == 3) {
            struct gsbc_item arg;

            arg = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[2]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, arg, pend, pc);
        } else {
            for (i = 2; i < item.v->numarguments; i += 2) {
                struct gsbc_item arg;

                arg = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[i+1]);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, arg, pend, pc);
            }
        }
    } else if (gssymceq(directive, gssymrune, gssymdatadirective, ".rune")) {
    } else if (gssymceq(directive, gssymstring, gssymdatadirective, ".string")) {
        static gsinterned_string gssymtylist, gssymtyrune;
        struct gsbc_item dest;

        if (!gssymtylist)
            gssymtylist = gsintern_string(gssymtypelable, "list.t")
        ;
        dest = gssymtable_lookup(item.v->pos, symtable, gssymtylist);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);

        if (!gssymtyrune)
            gssymtyrune = gsintern_string(gssymtypelable, "rune.t")
        ;
        dest = gssymtable_lookup(item.v->pos, symtable, gssymtyrune);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);

        if (item.v->numarguments > 1) {
            dest = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[1]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);
        }
    } else if (gssymceq(directive, gssymlist, gssymdatadirective, ".list")) {
        static gsinterned_string gssymtylist;
        struct gsbc_item dest;
        int i;

        if (!gssymtylist)
            gssymtylist = gsintern_string(gssymtypelable, "list.t")
        ;
        dest = gssymtable_lookup(item.v->pos, symtable, gssymtylist);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);

        for (i = 1; i < item.v->numarguments && item.v->arguments[i]->type != gssymseparator; i++) {
            dest = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[i]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);
        }
        if (i < item.v->numarguments) {
            gsfatal(UNIMPL("%P: Dotted .lists next"), item.v->pos);
        }
    } else if (gssymceq(directive, gssymregex, gssymdatadirective, ".regex")) {
        static gsinterned_string gssymtyregex, gssymtyrune;
        struct gsbc_item dest;
        int i;

        if (!gssymtyregex)
            gssymtyregex = gsintern_string(gssymtypelable, "regex.t")
        ;
        dest = gssymtable_lookup(item.v->pos, symtable, gssymtyregex);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);

        if (!gssymtyrune)
            gssymtyrune = gsintern_string(gssymtypelable, "rune.t")
        ;
        dest = gssymtable_lookup(item.v->pos, symtable, gssymtyrune);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);

        for (i = 1; i < item.v->numarguments; i++) {
            dest = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[i]);
            gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, dest, pend, pc);
        }
    } else if (gssymceq(directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gsbc_item ty;

        ty = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[0]);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, ty, pend, pc);
    } else if (gssymceq(directive, gssymcast, gssymdatadirective, ".cast")) {
        struct gsbc_item co, src;

        src = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[0]);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, src, pend, pc);
        co = gssymtable_lookup(item.v->pos, symtable, item.v->arguments[1]);
        gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, co, pend, pc);
    } else {
        gsfatal(UNIMPL("%P: gsbc_subtop_sort(data item; directive = %y)"), item.v->pos, item.v->directive);
    }
}

static
void
gsbc_top_sort_subitems_of_code_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    static gsinterned_string gssymexpr, gssymforcecont, gssymstrictcont, gssymubcasecont, gssymimpprog;
    static gsinterned_string gssymtygvar, gssymtyextabstype, gssymtyextelimprim, gssymtyarg, gssymtyfv, gssymtylet, gssymcogvar, gssymgvar, gssymnatural, gssymsubcode;

    struct gsparsedline *p;
    struct gsparsedfile_segment *pseg;

    pseg = item.pseg;
    if (
        gssymceq(item.v->directive, gssymexpr, gssymcodedirective, ".expr")
        || gssymceq(item.v->directive, gssymforcecont, gssymcodedirective, ".forcecont")
        || gssymceq(item.v->directive, gssymstrictcont, gssymcodedirective, ".strictcont")
        || gssymceq(item.v->directive, gssymubcasecont, gssymcodedirective, ".ubcasecont")
        || gssymceq(item.v->directive, gssymimpprog, gssymcodedirective, ".impprog")
    ) {
        for (p = gsinput_next_line(&pseg, item.v); ; p = gsinput_next_line(&pseg, p)) {
            if (
                gssymceq(p->directive, gssymtygvar, gssymcodeop, ".tygvar")
                || gssymceq(p->directive, gssymtyextabstype, gssymcodeop, ".tyextabstype")
            ) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (
                gssymceq(p->directive, gssymtyextelimprim, gssymcodeop, ".tyextelimprim")
                || gssymceq(p->directive, gssymtyarg, gssymcodeop, ".tyarg")
                || gssymceq(p->directive, gssymtyfv, gssymcodeop, ".tyfv")
                || gssymceq(p->directive, gssymtylet, gssymcodeop, ".tylet")
            ) {
            } else if (gssymceq(p->directive, gssymcogvar, gssymcodeop, ".cogvar")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (gssymceq(p->directive, gssymgvar, gssymcodeop, ".gvar")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (gssymceq(p->directive, gssymnatural, gssymcodeop, ".natural")) {
            } else if (gssymceq(p->directive, gssymsubcode, gssymcodeop, ".subcode")) {
                struct gsbc_item subcode;
                subcode = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, subcode, pend, pc);
            } else {
                return;
            }
        }
    } else {
        gsfatal(UNIMPL("%P: gsbc_subtop_sort(directive = %y)"), item.v->pos, item.v->directive);
    }

    gsfatal(UNIMPL("gsbc_subtop_sort_code_item"));
}

static
void
gsbc_top_sort_subitems_of_type_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    static gsinterned_string gssymtyexpr, gssymtyabstract, gssymtydefinedprim, gssymtyintrprim, gssymtyelimprim, gssymtyimpprim;
    static gsinterned_string gssymtygvar, gssymtyextabstype, gssymtyextelimprim, gssymtyextimpprim;

    gsinterned_string directive;
    struct gsparsedfile_segment *pseg;
    struct gsparsedline *ptype;

    pseg = item.pseg;
    ptype = item.v;
    directive = ptype->directive;
    if (
        gssymceq(directive, gssymtyexpr, gssymtypedirective, ".tyexpr")
        || gssymceq(directive, gssymtyabstract, gssymtypedirective, ".tyabstract")
    ) {
        struct gsparsedline *p;
        for (p = gsinput_next_line(&pseg, ptype); ; p = gsinput_next_line(&pseg, p)) {
            if (gssymceq(p->directive, gssymtygvar, gssymtypeop, ".tygvar")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (gssymceq(p->directive, gssymtyextabstype, gssymtypeop, ".tyextabstype")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (
                gssymceq(p->directive, gssymtyextelimprim, gssymtypeop, ".tyextelimprim")
                || gssymceq(p->directive, gssymtyextimpprim, gssymtypeop, ".tyextimpprim")
            ) {
                /* skipped */
            } else {
                return;
            }
        }
    } else if (
        gssymceq(directive, gssymtydefinedprim, gssymtypedirective, ".tydefinedprim")
        || gssymceq(directive, gssymtyintrprim, gssymtypedirective, ".tyintrprim")
        || gssymceq(directive, gssymtyelimprim, gssymtypedirective, ".tyelimprim")
        || gssymceq(directive, gssymtyimpprim, gssymtypedirective, ".tyimpprim")
    ) {
        struct gsregistered_primset *prims;
        struct gsregistered_primtype *type;
        enum gsprim_type_group expected_group;
        char *expected_group_descr;

        if (ptype->numarguments < 1)
            gsfatal("P: Missing primitive set name on %y", ptype->pos, directive)
        ;
        prims = gsprims_lookup_prim_set(ptype->arguments[0]->name);
        if (directive == gssymtydefinedprim && !prims)
            gsfatal("%P: Unknown primitive set %y, which is really bad since it's supposed to contain defined types", ptype->pos, ptype->arguments[0])
        ;

        if (ptype->numarguments < 2)
            gsfatal("%P: Missing primitive type name on %y", ptype->pos, directive)
        ;
        if (prims && !(type = gsprims_lookup_type(prims, ptype->arguments[1]->name)))
            gsfatal("%P: Primitive set %y does not in fact contain a type %y", ptype->pos, ptype->arguments[0], ptype->arguments[1])
        ;
        if (directive == gssymtydefinedprim) {
            expected_group = gsprim_type_defined;
            expected_group_descr = "a defined type";
        } else if (directive == gssymtyintrprim) {
            expected_group = gsprim_type_intr;
            expected_group_descr = "an intrtype";
        } else if (directive == gssymtyelimprim) {
            expected_group = gsprim_type_elim;
            expected_group_descr = "an elimtype";
        } else if (directive == gssymtyimpprim) {
            expected_group = gsprim_type_imp;
            expected_group_descr = "an imptype";
        } else {
            gsfatal(UNIMPL("%P: Check primtype group of %y against registered primtype group"), ptype->pos, directive);
        }
        if (type && type->prim_type_group != expected_group)
            gsfatal("%P: Primitive type %y in primitive set %y is not in fact %s", ptype->pos, ptype->arguments[1], ptype->arguments[0], expected_group_descr)
        ;

        if (ptype->numarguments < 3)
            gsfatal("%P: Missing kind on %y", ptype->pos, directive)
        ;

        if (ptype->numarguments > 3)
            gsfatal("%P: Too many arguments to %y", ptype->pos, directive)
        ;
        return;
    } else {
        gsfatal(UNIMPL("%P: gsbc_subtop_sort(directive = %y)"), ptype->pos, ptype->directive);
    }

    gsfatal(UNIMPL("%P: gsbc_subtop_sort_type_item"), ptype->pos);
}

static
void
gsbc_top_sort_subitems_of_coercion_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    static gsinterned_string gssymtycoercion, gssymtygvar, gssymtyextabstype, gssymtyextelimprim, gssymtyextimpprim;

    gsinterned_string directive;
    struct gsparsedfile_segment *pseg;
    struct gsparsedline *pcoercion;

    pseg = item.pseg;
    pcoercion = item.v;
    directive = pcoercion->directive;
    if (gssymceq(directive, gssymtycoercion, gssymcoerciondirective, ".tycoercion")) {
        struct gsparsedline *p = gsinput_next_line(&pseg, pcoercion);
        for (; ; p = gsinput_next_line(&pseg, p)) {
            if (gssymceq(p->directive, gssymtygvar, gssymcoercionop, ".tygvar")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (gssymceq(p->directive, gssymtyextabstype, gssymcoercionop, ".tyextabstype")) {
                struct gsbc_item global;
                global = gssymtable_lookup(p->pos, symtable, p->label);
                gsbc_topsort_outgoing_edge(symtable, preorders, unassigned_items, maybe_group_items, global, pend, pc);
            } else if (
                gssymceq(p->directive, gssymtyextelimprim, gssymcoercionop, ".tyextelimprim")
                || gssymceq(p->directive, gssymtyextimpprim, gssymcoercionop, ".tyextimpprim")
            ) {
                /* skipped */
            } else {
                return;
            }
        }
    } else {
        gsfatal(UNIMPL("%P: gsbc_subtop_sort(directive = %y)"), pcoercion->pos, pcoercion->directive);
    }

    gsfatal(UNIMPL("%P: gsbc_subtop_sort_type_item"), pcoercion->pos);
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

static ulong gsbc_item_hash_value(struct gsbc_item);

static
int
gsbc_item_hash_lookup(struct gsbc_item_hash *phash, struct gsbc_item item, union gsbc_item_hash_value *pres)
{
    ulong hash_value;
    struct gsbc_item_hash_link *p;

    if (!phash) gsfatal("Forgot to allocate hash");

    hash_value = gsbc_item_hash_value(item);

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

static struct gs_sys_global_block_suballoc_info gsbc_hash_link_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Byte-compiler hash links",
    },
};

static
void
gsbc_item_hash_store(struct gsbc_item_hash *phash, struct gsbc_item item, union gsbc_item_hash_value v)
{
    ulong hash_value;
    struct gsbc_item_hash_link *p, **plastp;

    if (!phash) gsfatal("Forgot to allocate hash");

    hash_value = gsbc_item_hash_value(item);

    plastp = &phash->buckets[hash_value % phash->nbuckets];
    for (p = phash->buckets[hash_value % phash->nbuckets]; p; p = p->next) {
        if (gsbc_item_eq(p->key, item)) {
            p->value = v;
            return;
        }
        plastp = &p->next;
    }

    *plastp = p = gs_sys_global_block_suballoc(&gsbc_hash_link_info, sizeof(struct gsbc_item_hash_link));

    p->next = 0;
    p->key = item;
    p->value = v;
}

ulong
gsbc_item_hash_value(struct gsbc_item item)
{
    ulong hash_value;

    hash_value = (uintptr)item.file->name->hash;
    hash_value = hash_value * 33 + (uintptr)item.type;
    hash_value = hash_value * 33 + (item.v->label ? item.v->label->hash : 0);

    return hash_value;
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

static struct gs_sys_global_block_suballoc_info gsbc_scc_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Byte-compiler SCCs",
    },
};

static
struct gsbc_scc *
gsbc_alloc_scc(struct gsbc_item item)
{
    struct gsbc_scc *res;

    res = gs_sys_global_block_suballoc(&gsbc_scc_info, sizeof(*res));

    res->item = item;
    res->next_item = 0;
    res->next_scc = 0;

    return res;
}

int
gsbc_item_eq(struct gsbc_item item0, struct gsbc_item item1)
{
    return
        item0.file == item1.file
        && item0.type == item1.type
        && item0.v == item1.v
    ;
}

void
gsbc_item_empty(struct gsbc_item *pitem)
{
    pitem->file = 0;
    pitem->type = 0;
    pitem->pseg = 0;
    pitem->v = 0;
}

struct gs_sys_global_block_suballoc_info gsbc_item_hash_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Byte compile item-keyed hashes",
    },
};

#define GSBC_INITIAL_BUCKET_COUNT 32

static
struct gsbc_item_hash *
gsbc_alloc_item_hash()
{
    struct gsbc_item_hash *res;
    ulong alloc_size;
    struct gsbc_item_hash_link **p;

    alloc_size = sizeof(*res) + GSBC_INITIAL_BUCKET_COUNT * sizeof(res->buckets[0]);

    res = gs_sys_global_block_suballoc(&gsbc_item_hash_info, alloc_size);

    res->size = 0;
    res->nbuckets = GSBC_INITIAL_BUCKET_COUNT;
    for (p = res->buckets; p < res->buckets + res->nbuckets; p++)
        *p = 0
    ;

    return res;
}
