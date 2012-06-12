/* §source.file{Byte-compiler} */

/* NB: We also do type-checking of gsac files in this file */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsbytecompile.h"

struct gsbc_scc {
    struct gsbc_item item;
    struct gsbc_scc *next;
};

static struct gsbc_scc *gsbc_topsortfile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable);

void
gsbytecompile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, gsvalue *pentry)
{
    struct gsbc_scc *pscc;

    switch (parsedfile->type) {
        case gsfiledocument:
            pscc = gsbc_topsortfile(parsedfile, symtable);
            gsfatal("%s: gsbytecompile(document) next", parsedfile->name->name);
        default:
            gsfatal("%s: Unknown file type %d in gsbytecompile", parsedfile->name->name, parsedfile->type);
    }
}

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

static void gsbc_top_sort_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_stack_initialize(struct gsbc_item_stack *);

static
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
        case gsfiledocument: {
            struct gsbc_item item;
            item.file = parsedfile;
            item.type = gssymdatalable;
            item.v.pdata = GSDATA_SECTION_FIRST_ITEM(parsedfile->data);
            if (!parsedfile->data)
                gsfatal("%s: Cannot compile file: no data section so no entry points", parsedfile->name->name);
            if (!parsedfile->data->numitems)
                gsfatal("%s: Document lacks first data item; cannot find entry point", parsedfile->name->name);
            gsbc_top_sort_item(symtable, preorders, &unassigned_items, &maybe_group_items, item, &pend, &c);
            return res;
        }
        default:
            gsfatal("gsbc_topsortfile: %s: unknown file type %d", parsedfile->name->name, parsedfile->type);
    }

    gsfatal("gsbc_topsortfile(%s) next", parsedfile->name->name);
    return res;
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
static int gsbc_item_eq(struct gsbc_item, struct gsbc_item);

static void gsbc_top_sort_subitems_of_data_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_top_sort_subitems_of_code_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

static void gsbc_top_sort_subitems_of_type_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc);

/* Based on §url{http://en.wikipedia.org/wiki/Path-based_strong_component_algorithm} */

static
void
gsbc_top_sort_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
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
        struct gsbc_scc *pnew, **plast;
        struct gsbc_item last;
        plast = &pnew;
        gsbc_item_empty(&last);
        do {
            last = gsbc_pop(unassigned_items);
            *plast = gsbc_alloc_scc(last);
            plast = &(*plast)->next;
        } while (!gsbc_item_eq(last, item));
        **pend = pnew;
        *pend = &pnew->next;
        gsbc_pop(maybe_group_items);
    }
}

static
void
gsbc_subtop_sort(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    ulong n;

    if (n = gsbc_preorder_get(preorders, item)) {
        if (gsbc_stack_in(unassigned_items, item))
            while (gsbc_preorder_get(preorders, gsbc_top(maybe_group_items)) > n)
                gsbc_pop(maybe_group_items)
        ;
    } else {
        gsbc_top_sort_item(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
    }
}

enum gsbc_data_directive {
    gsdata_directive_closure,
    gsnum_data_directives,
};

static enum gsbc_data_directive gsbc_lookup_data_directive(gsinterned_string);

static
void
gsbc_top_sort_subitems_of_data_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    enum gsbc_data_directive directive;

    directive = gsbc_lookup_data_directive(item.v.pdata->directive);
    switch (directive) {
        case gsdata_directive_closure:
            {
                struct gsbc_item code, type;
                code = gssymtable_lookup(
                    item.v.pdata->file->name,
                    item.v.pdata->lineno,
                    symtable,
                    item.v.pdata->arguments[0]
                );
                gsbc_subtop_sort(symtable, preorders, unassigned_items, maybe_group_items, code, pend, pc);
                if (item.v.pdata->numarguments >= 2) {
                    type = gssymtable_lookup(
                        item.v.pdata->file->name,
                        item.v.pdata->lineno,
                        symtable,
                        item.v.pdata->arguments[1]
                    );
                    gsbc_subtop_sort(symtable, preorders, unassigned_items, maybe_group_items, type, pend, pc);
                }
                if (item.v.pdata->numarguments > 2)
                    gsfatal("%s:%d: Panic: data item %s:%s has more than two arguments; I don't know what to do!",
                        __FILE__,
                        __LINE__,
                        item.file->name->name,
                        item.v.pdata->label->name
                    )
                ;
            }
            break;
            gsfatal("%s: gsbc_subtop_sort(closure) next", item.file->name->name);
        default:
            gsfatal("%s: gsbc_subtop_sort(directive = %s) next", item.file->name->name, item.v.pdata->directive->name);
    }
}

struct {
    char *name;
    gsinterned_string interned;
} gsbc_data_directive_table[] = {
    /* gsdata_directive_closure = */ { ".closure", 0, },
    { 0, 0, },
};

static
enum gsbc_data_directive
gsbc_lookup_data_directive(gsinterned_string dir)
{
    enum gsbc_data_directive i;

    for (i = 0; i < gsnum_data_directives; i++) {
        if (!gsbc_data_directive_table[i].name)
            gsfatal("Missing items in gsbc_data_directive_table; only %d items", i);
        if (!gsbc_data_directive_table[i].interned)
            gsbc_data_directive_table[i].interned = gsintern_string(gssymdatadirective, gsbc_data_directive_table[i].name);
        if (gsbc_data_directive_table[i].interned == dir)
            return i;
    }

    gsfatal("%s: Unknown directive", dir->name);

    return -1;
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
                            gsbc_subtop_sort(symtable, preorders, unassigned_items, maybe_group_items, item, pend, pc);
                        }
                        break;
                    default:
                        return;
                }
            }
            break;
        default:
            gsfatal("%s: gsbc_subtop_sort(directive = %s) next", item.file->name->name, item.v.pcode->directive->name);
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

static
void
gsbc_top_sort_subitems_of_type_item(struct gsfile_symtable *symtable, struct gsbc_item_hash *preorders, struct gsbc_item_stack *unassigned_items, struct gsbc_item_stack *maybe_group_items, struct gsbc_item item, struct gsbc_scc ***pend, ulong *pc)
{
    gsinterned_string directive;
    struct gsparsedline *p;

    directive = item.v.ptype->directive;
    if (gssymeq(directive, gssymtypedirective, ".tyexpr")) {
        for (p = gsinput_next_line(item.v.pcode); ; p = gsinput_next_line(p)) {
            if (0) {
            } else {
                return;
            }
        }
        gsfatal("%s:%d: %s:%d: gsbc_subtop_sort(type expressions) next", __FILE__, __LINE__, item.file->name->name, item.v.ptype->lineno);
    } else {
        gsfatal("%s:%d: gsbc_subtop_sort(directive = %s) next", item.file->name->name, item.v.ptype->lineno, item.v.ptype->directive->name);
    }

    gsfatal("%s:%d: gsbc_subtop_sort_type_item next", __FILE__, __LINE__);
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
        gsfatal("Expand stack next");

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
    res->next = 0;

    return res;
}

static
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
