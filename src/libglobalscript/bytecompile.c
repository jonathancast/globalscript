/* §source.file{Byte-compiler} */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsregtables.h"
#include "gstypealloc.h"
#include "gstypecheck.h"
#include "gsbytecompile.h"
#include "gsheap.h"
#include "gsbytecode.h"
#include "gstopsort.h"

enum gsbc_heap_section {
    gsbc_non_data,
    gsbc_indir,
    gsbc_heap,
    gsbc_errors,
    gsbc_records,
    gsbc_constrs,
    gsbc_num_heap_sections,
};

static void gsbc_size_data_item(struct gsfile_symtable *, struct gsbc_item, enum gsbc_heap_section *, uint *, gsvalue *);

void
gsbc_alloc_data_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, int n)
{
    uint total_size[gsbc_num_heap_sections];
    enum gsbc_heap_section sections[MAX_ITEMS_PER_SCC];
    uint offsets[MAX_ITEMS_PER_SCC], sizes[MAX_ITEMS_PER_SCC];
    gsvalue indirs[MAX_ITEMS_PER_SCC];
    int i;
    enum gsbc_heap_section s;
    void *bases[gsbc_num_heap_sections];

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    for (s = 0; s < gsbc_num_heap_sections; s++)
        total_size[s] = 0
    ;
    for (i = 0; i < n; i++) {
        gsbc_size_data_item(symtable, items[i], &sections[i], &sizes[i], &indirs[i]);

        if (sections[i] != gsbc_non_data && sections[i] != gsbc_indir) {
            offsets[i] = total_size[sections[i]];
            total_size[sections[i]] += sizes[i];
            if (total_size[sections[i]] % sizeof(void *))
                total_size[sections[i]] += sizeof(void *) - (total_size[sections[i]] % sizeof(void *))
            ;
        }
    }

    for (s = 0; s < gsbc_num_heap_sections; s++) {
        if (total_size[s] > BLOCK_SIZE)
            gsfatal(UNIMPL("Total size too large for section %d; 0x%x > 0x%x"), s, total_size[s], BLOCK_SIZE)
        ;
    }

    bases[gsbc_heap] = gsreserveheap(total_size[gsbc_heap]);
    bases[gsbc_errors] = gsreserveerrors(total_size[gsbc_errors]);
    bases[gsbc_records] = gsreserverecords(total_size[gsbc_records]);
    bases[gsbc_constrs] = gsreserveconstrs(total_size[gsbc_constrs]);

    for (i = 0; i < n; i++) {
        if (sections[i] == gsbc_non_data) {
            heap[i] = 0;
        } else if (sections[i] == gsbc_indir) {
            heap[i] = indirs[i];
        } else {
            heap[i] = (gsvalue)((uchar*)bases[sections[i]] + offsets[i]);
        }
        if (heap[i])
            gssymtable_set_data(symtable, items[i].v->label, heap[i])
        ;
    }
}

static char *gsbc_parse_rune_literal(struct gspos, uint, char *, gsvalue *);

static gsinterned_string gssymundefined, gssymrecord, gssymconstr, gssymrune, gssymstring, gssymlist, gssymregex, gssymclosure, gssymalias, gssymcast;

static
void
gsbc_size_data_item(struct gsfile_symtable *symtable, struct gsbc_item item, enum gsbc_heap_section *psection, uint *psize, gsvalue *pindir)
{
    struct gsparsedline *p;

    if (item.type != gssymdatalable) {
        *psection = gsbc_non_data;
        return;
    }

    p = item.v;
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        *psection = gsbc_records;
        *psize = sizeof(struct gsrecord_fields) + (p->numarguments / 2) * sizeof(gsvalue);
    } else if (gssymceq(p->directive, gssymconstr, gssymdatadirective, ".constr")) {
        *psection = gsbc_constrs;
        if (p->numarguments == 3) {
            *psize = sizeof(struct gsconstr) + sizeof(gsvalue);
        } else {
            *psize = sizeof(struct gsconstr) + (p->numarguments / 2) * sizeof(gsvalue);
        }
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        char *eos;
        gsvalue res;

        eos = gsbc_parse_rune_literal(p->pos, item.file->features, p->arguments[0]->name, &res);
        if (*eos)
            gsfatal("%P: %s: More than one rune in argument to .rune", p->pos, p->arguments[0]->name)
        ;

        *psection = gsbc_indir;
        *pindir = res;
    } else if (gssymceq(p->directive, gssymstring, gssymdatadirective, ".string")) {
        char *eos;
        int len;
        ulong size;

        *psection = gsbc_constrs;
        eos = p->arguments[0]->name;
        len = 0;
        while (*eos) {
            gsvalue v;

            eos = gsbc_parse_rune_literal(p->pos, item.file->features, eos, &v);
            len++;
        }
        size = len * (sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        if (p->numarguments < 2)
            size += sizeof(struct gsconstr)
        ;
        *psize = size;
    } else if (gssymceq(p->directive, gssymlist, gssymdatadirective, ".list")) {
        if (p->numarguments >= 2 && p->arguments[p->numarguments - 2]->type == gssymseparator)
            gsfatal(UNIMPL("%P: Dotted .lists"), p->pos)
        ;
        *psection = gsbc_constrs;
        *psize = p->numarguments * (sizeof(struct gsconstr) + 2 * sizeof(gsvalue)) + sizeof(struct gsconstr);
    } else if (gssymceq(p->directive, gssymregex, gssymdatadirective, ".regex")) {
        int interp;
        char *eos;
        ulong size;
        gsvalue v;

        interp = 1;
        size = sizeof(struct gsconstr) + 2*sizeof(gsvalue) + sizeof(struct gsconstr); /* sum + null */
        eos = p->arguments[0]->name;

        if (*eos == '|') size += sizeof(struct gsconstr);
        while (*eos) {
            if (*eos == '|') {
                size += sizeof(struct gsconstr) + 2*sizeof(gsvalue);
                eos++;
                if (!*eos || *eos == '|') size += sizeof(struct gsconstr);
            } else {
                if (*eos == '[') {
                    eos++;
                    size += sizeof(struct gsconstr) + sizeof(gsvalue);
                    if (*eos == '^') eos++;

                    if (*eos == ']') size += sizeof(struct gsconstr);
                    else while (*eos != ']') {
                        eos = gsbc_parse_rune_literal(p->pos, item.file->features, eos, &v);
                        if (!*eos)
                            gsfatal(UNIMPL("%P: %s: Character classes"), p->pos, eos)
                        ; else if (*eos == '-') {
                            eos++;
                            if (!*eos) gsfatal(UNIMPL("%P: %s: Character classes"), p->pos, eos);
                            eos = gsbc_parse_rune_literal(p->pos, item.file->features, eos, &v);
                            size += sizeof(struct gsconstr) + 2 * sizeof(gsvalue);
                        } else {
                            size += sizeof(struct gsconstr) + sizeof(gsvalue);
                        }
                        if (*eos != ']') size += sizeof(struct gsconstr) + 2 * sizeof(gsvalue);
                    }
                    eos++;
                } else if (eos[0] == '\xc2' && eos[1] == '\xa7') { /* \§ */
                    eos += 2;
                    if (interp >= p->numarguments) gsfatal("%P: More interpolation locations than values", p->pos);
                    interp++;
                } else {
                    eos = gsbc_parse_rune_literal(p->pos, item.file->features, eos, &v);
                    size += sizeof(struct gsconstr) + 1 * sizeof(gsvalue);
                }
                if (*eos == '*') {
                    size += sizeof(struct gsconstr) + sizeof(gsvalue);
                    eos++;
                }
                if (*eos && *eos != '|') size += sizeof(struct gsconstr) + 2*sizeof(gsvalue);
            }
        }

        *psection = gsbc_constrs;
        *psize = size;

        if (interp < p->numarguments) gsfatal("%P: More interpolation values than locations", p->pos);
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        *psection = gsbc_errors;
        *psize = MAX(sizeof(struct gserror), sizeof(struct gserror_forward));
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        *psection = gsbc_heap;
        *psize = MAX(sizeof(struct gsclosure), sizeof(struct gsindirection));
    } else if (
        gssymceq(p->directive, gssymalias, gssymdatadirective, ".alias")
        || gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")
    ) {
        gsvalue res;

        res = gssymtable_get_data(symtable, p->arguments[0]);
        if (!res) gsfatal(UNIMPL("%P: Can't find cast referent %y"), p->pos, p->arguments[0]);
        *psection = gsbc_indir;
        *pindir = res;
    } else {
        gsfatal(UNIMPL("%P: gsbc_heap_size_item(%y)"), item.v->pos, p->directive);
    }
}

char *
gsbc_parse_rune_literal(struct gspos pos, uint features, char *s, gsvalue *pv)
{
    int noctets;

    *pv = 0;

    if (*s == '\\') {
        switch (s[1]) {
            case 's':
                *pv = ' ';
                break;
            case 't':
                *pv = '\t';
                break;
            case 'n':
                *pv = '\n';
                break;
            case '*':
            case '-':
            case '.':
            case '?':
            case '\\':
            case '|':
            case '[':
            case ']':
                *pv = s[1];
                break;
            default:
                gsfatal(UNIMPL("%P: gsbc_parse_rune_literal(%s)"), pos, s);
        }
        s += 2;
    } else {
        if ((*s & 0x80) == 0) {
            noctets = 1;
        } else if ((*s & 0xe0) == 0xc0) {
            noctets = 2;
        } else if ((*s & 0xf0) == 0xe0) {
            noctets = 3;
        } else {
            noctets = 0;
            gsfatal(UNIMPL("%P: gsbc_parse_rune_literal(%s)"), pos, s);
        }

        while (noctets--) {
            *pv <<= 8;
            *pv |= (uchar)*s++;
        }
    }

    *pv |= GS_MAX_PTR;

    return s;
}

static int gsbc_bytecode_size_item(struct gsfile_symtable *, struct gsbc_item item);

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
        size = items[i].type == gssymcodelable ? gsbc_bytecode_size_item(symtable, items[i]) : 0;
        total_size += size;
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gsreservebytecode(total_size);

    for (i = 0; i < n; i++) {
        if (items[i].type == gssymcodelable) {
            bcos[i] = (struct gsbco*)((uchar*)base + offsets[i]);
            bcos[i]->size = i + 1 < n ? offsets[i + 1] - offsets[i] : total_size - offsets[i];
            gssymtable_set_code(symtable, items[i].v->label, bcos[i]);
        }
    }
}

struct gsbc_bytecode_size_code_closure {
    uint features;

    int size;

    int nregs;

    int ncodes;
    gsinterned_string codenames[MAX_NUM_REGISTERS];
    struct gsbc_code_item_type *codetypes[MAX_NUM_REGISTERS];
};
static int gsbc_bytecode_size_code_type_gvar(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_code_type_fv(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_code_type_arg(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_cont_type_arg(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_code_type_let_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_code_subcode_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_coercion_gvar_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_data_gvar_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_data_fv_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_cont_arg_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_arg_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_alloc_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_cast_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_cont_push_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_terminal_code_op(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_bytecode_size_code_closure *);

/* Type free variables */
static gsinterned_string gssymoptyextabstype, gssymoptyextelimprim, gssymoptygvar, gssymoptyfv;
/* Type arguments */
static gsinterned_string gssymoptyarg;
/* Type allocations */
static gsinterned_string gssymoptylet;
/* Code/coercion/data free variables */
static gsinterned_string gssymopsubcode, gssymopcogvar, gssymopgvar, gssymoprune, gssymopnatural, gssymopfv, gssymopefv;
/* Data arguments */
static gsinterned_string gssymoparg, gssymoplarg, gssymopexkarg, gssymopkarg, gssymopfkarg;
/* Allocation */
static gsinterned_string gssymopclosure, gssymopprim, gssymopconstr, gssymopexconstr, gssymoprecord, gssymoplrecord, gssymopfield, gssymoplfield, gssymopundefined, gssymopappty, gssymopcast, gssymoplifted, gssymopapply, gssymopimpprim;
/* Continuations */
static gsinterned_string gssymoplift, gssymoptyapp, gssymopcoerce, gssymopapp, gssymopforce, gssymopstrict, gssymopubanalyze;
/* Terminals */
static gsinterned_string gssymopyield, gssymopenter, gssymopubprim, gssymoplprim, gssymopundef;
/* Branching */
static gsinterned_string gssymopanalyze, gssymopdanalyze, gssymopcase, gssymopdefault;
/* API */
static gsinterned_string gssymopbind, gssymopbody;

static
int
gsbc_bytecode_size_item(struct gsfile_symtable *symtable, struct gsbc_item item)
{
    struct gsbc_bytecode_size_code_closure cl;

    int i;
    struct gsparsedline *p;
    struct gsparsedfile_segment *pseg;

    cl.features = item.file->features;
    cl.size = sizeof(struct gsbco);

    cl.nregs = cl.ncodes = 0;
    for (i = 0; i < MAX_NUM_REGISTERS; i++) {
        cl.codenames[i] = 0;
        cl.codetypes[i] = 0;
    }
    pseg = item.pseg;
    p = gsinput_next_line(&pseg, item.v);

    /* §paragraph{Global Variables} */
    while (gsbc_bytecode_size_code_type_gvar(p, &cl)) p = gsinput_next_line(&pseg, p);
    while (gsbc_bytecode_size_code_subcode_op(symtable, p, &cl)) p = gsinput_next_line(&pseg, p);
    while (gsbc_bytecode_size_coercion_gvar_code_op(p, &cl)) p = gsinput_next_line(&pseg, p);
    while (gsbc_bytecode_size_data_gvar_code_op(p, &cl)) p = gsinput_next_line(&pseg, p);

    /* §paragraph{Free Variables} */
    while (gsbc_bytecode_size_code_type_fv(p, &cl)) p = gsinput_next_line(&pseg, p);
    while (gsbc_bytecode_size_code_type_let_op(p, &cl)) p = gsinput_next_line(&pseg, p);
    while (gsbc_bytecode_size_data_fv_code_op(p, &cl)) p = gsinput_next_line(&pseg, p);

    /* §paragraph{Arguments} */
    while (
        gsbc_bytecode_size_cast_op(p, &cl)
        || gsbc_bytecode_size_code_type_arg(p, &cl)
        || gsbc_bytecode_size_code_type_let_op(p, &cl)
        || gsbc_bytecode_size_cont_arg_code_op(p, &cl)
        || gsbc_bytecode_size_arg_code_op(p, &cl)
    )
        p = gsinput_next_line(&pseg, p)
    ;

    /* §paragraph{Expression} */
    while (gsbc_bytecode_size_alloc_op(p, &cl)) p = gsinput_next_line(&pseg, p);
    while (
        gsbc_bytecode_size_cast_op(p, &cl)
        || gsbc_bytecode_size_cont_push_op(p, &cl)
    )
        p = gsinput_next_line(&pseg, p)
    ;
    if (!gsbc_bytecode_size_terminal_code_op(&pseg, &p, &cl)) {
        gsfatal(UNIMPL("%P: gsbc_bytecode_size_item (%y)"), p->pos, p->directive);
    }

    if (cl.size % sizeof(void*))
        cl.size += sizeof(void*) - cl.size % sizeof(void*)
    ;

    return cl.size;
}

int
gsbc_bytecode_size_code_type_gvar(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymoptyextabstype, gssymcodeop, ".tyextabstype")
        || gssymceq(p->directive, gssymoptyextelimprim, gssymcodeop, ".tyextelimprim")
        || gssymceq(p->directive, gssymoptygvar, gssymcodeop, ".tygvar")
    ) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_code_type_fv(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymoptyfv, gssymcodeop, ".tyfv")) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_code_type_arg(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymoptyarg, gssymcodeop, ".tyarg")) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_cont_type_arg(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopexkarg, gssymcodeop, ".exkarg")) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_bytecode_size_code_type_let_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymoptylet, gssymcodeop, ".tylet")) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_code_subcode_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopsubcode, gssymcodeop, ".subcode")) {
        if (pcl->ncodes >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many sub-expressions; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (pcl->size % sizeof(struct gsbco *))
            gsfatal(UNIMPL("%P: File format error: we're at a .subcode generator but our location isn't struct gsbco *-aligned"), p->pos)
        ;
        pcl->size += sizeof(struct gsbco *);
        pcl->codenames[pcl->ncodes] = p->label;
        pcl->codetypes[pcl->ncodes] = gssymtable_get_code_type(symtable, p->label);
        pcl->ncodes++;
    } else {
        return 0;
    }
    return 1;
}

#define CHECK_PHASE(ph, nm) \
    do { \
        if (pcl->phase > ph) \
            gsfatal("Too late to add " nm, p->pos); \
        pcl->phase = ph; \
    } while (0)

int
gsbc_bytecode_size_coercion_gvar_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopcogvar, gssymcodeop, ".cogvar")) {
        /* type erasure */
    } else {
        return 0;
    }
    return 1;
}

void static gsbc_bytecode_size_check_natural_fits_in_one_word(struct gspos, gsinterned_string);

int
gsbc_bytecode_size_data_gvar_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopgvar, gssymcodeop, ".gvar")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (pcl->size % sizeof(gsvalue))
            gsfatal(UNIMPL("%P: File format error: we're at a .gvar generator but our location isn't gsvalue-aligned"), p->pos)
        ;
        pcl->size += sizeof(gsvalue);
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymoprune, gssymcodeop, ".rune")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (pcl->size % sizeof(gsvalue))
            gsfatal(UNIMPL("%P: File format error: we're at a .rune generator but our location isn't gsvalue-aligned"), p->pos)
        ;
        pcl->size += sizeof(gsvalue);
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymopnatural, gssymcodeop, ".natural")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        if (pcl->size % sizeof(gsvalue))
            gsfatal(UNIMPL("%P: File format error: we're at a .rune generator but our location isn't gsvalue-aligned"), p->pos)
        ;
        gsbc_bytecode_size_check_natural_fits_in_one_word(p->pos, p->arguments[0]);
        pcl->size += sizeof(gsvalue);
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_data_fv_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopfv, gssymcodeop, ".fv")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymopefv, gssymcodeop, ".efv")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->size += ACE_EFV_SIZE();
        pcl->nregs++;
    } else {
        return 0;
    }
    return 1;
}

void
static
gsbc_bytecode_size_check_natural_fits_in_one_word(struct gspos pos, gsinterned_string d)
{
    static intptr max_before_division = (GS_MAX_PTR - 9) / 10;

    char *s;
    int n;

    n = 0;
    for (s = d->name; *s; s++) {
        int dig;

        if (!isdigit(*s))
            gsfatal("%P: Invalid character '%c' in natural number literal", pos, *s)
        ;
        dig = *s - '0';
        if (n > max_before_division)
            gsfatal("%P: Natural number %y too large", pos, d)
        ;
        n *= 10;
        if (n >= GS_MAX_PTR - dig)
            gsfatal("%P: Natural number %y too large", pos, d)
        ;
        n += dig;
    }
}

int
gsbc_bytecode_size_cont_arg_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymopkarg, gssymcodeop, ".karg")
        || gssymceq(p->directive, gssymopfkarg, gssymcodeop, ".fkarg")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;
    } else {
        return 0;
    }

    return 1;
}

int
gsbc_bytecode_size_arg_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymoparg, gssymcodeop, ".arg")
        || gssymceq(p->directive, gssymoplarg, gssymcodeop, ".larg")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;
    } else {
        return 0;
    }

    return 1;
}

#define CHECK_REGISTERS() \
    do { \
        if (pcl->nregs >= MAX_NUM_REGISTERS) \
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS) \
        ; \
        pcl->nregs++; \
    } while(0)

static
int
gsbc_bytecode_size_alloc_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    int i;

    if (gssymceq(p->directive, gssymopclosure, gssymcodeop, ".closure")) {
        int creg;
        struct gsbc_code_item_type *cty;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        creg = gsbc_find_register(p, pcl->codenames, pcl->ncodes, p->arguments[0]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0])
        ;

        pcl->size += ACE_CLOSURE_SIZE(cty->numfvs); /* Code reg + nfvs + fvs */
    } else if (gssymceq(p->directive, gssymopprim, gssymcodeop, ".prim")) {
        struct gsregistered_primset *prims;

        CHECK_REGISTERS();

        if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
            int nargs;

            /* Ignore type arguments & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nargs = p->numarguments - i;

            pcl->size += ACE_PRIM_SIZE(nargs); /* API index + # args + args */
        } else {
            pcl->size += ACE_UNKNOWN_PRIM_SIZE();
        }
    } else if (gssymceq(p->directive, gssymopimpprim, gssymcodeop, ".impprim")) {
        struct gsregistered_primset *prims;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
            int nargs;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nargs = p->numarguments - i;

            pcl->size += ACE_IMPPRIM_SIZE(nargs);
        } else {
            pcl->size += GS_SIZE_BYTECODE(0);
        }
    } else if (
        gssymceq(p->directive, gssymopconstr, gssymcodeop, ".constr")
        || gssymceq(p->directive, gssymopexconstr, gssymcodeop, ".exconstr")
    ) {
        int first_val_arg = 2;
        if (p->directive == gssymopexconstr) {
            /* Skip type arguments */
            for (i = first_val_arg; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            first_val_arg = i;
        }
        if (p->numarguments == first_val_arg + 1)
            pcl->size += GS_SIZE_BYTECODE(2 + 1)
        ; else
            pcl->size += GS_SIZE_BYTECODE(2 + (p->numarguments - first_val_arg) / 2)
        ;
    } else if (
        gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")
        || gssymceq(p->directive, gssymoplrecord, gssymcodeop, ".lrecord")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;
        for (i = 0; i < p->numarguments && p->arguments[i]->type != gssymseparator; i ++);

        pcl->size += ACE_SIZE_RECORD(i / 2);
    } else if (gssymceq(p->directive, gssymopfield, gssymcodeop, ".field")) {
        CHECK_REGISTERS();

        pcl->size += ACE_FIELD_SIZE();
    } else if (gssymceq(p->directive, gssymoplfield, gssymcodeop, ".lfield")) {
        CHECK_REGISTERS();

        pcl->size += ACE_LFIELD_SIZE();
    } else if (gssymceq(p->directive, gssymopundefined, gssymcodeop, ".undefined")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        pcl->size += ACE_UNDEFINED_SIZE();
    } else if (
        gssymceq(p->directive, gssymopappty, gssymcodeop, ".appty")
        || gssymceq(p->directive, gssymopcast, gssymcodeop, ".cast")
        || gssymceq(p->directive, gssymoplifted, gssymcodeop, ".lifted")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        pcl->size += ACE_ALIAS_SIZE(); /* FIXME: Should be erased */
    } else if (gssymceq(p->directive, gssymopapply, gssymcodeop, ".apply")) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        /* Skip type arguments */
        for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        if (i < p->numarguments) i++;

        pcl->size += ACE_APPLY_SIZE(p->numarguments - i);
    } else if (
        gssymceq(p->directive, gssymopbind, gssymcodeop, ".bind")
        && gssymceq(p->arguments[0], gssymopclosure, gssymcodeop, ".closure")
    ) {
        int creg;
        struct gsbc_code_item_type *cty;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        creg = gsbc_find_register(p, pcl->codenames, pcl->ncodes, p->arguments[1]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1])
        ;

        pcl->size += ACE_BIND_CLOSURE_SIZE(cty->numfvs);
    } else if (
        gssymceq(p->directive, gssymopbind, gssymcodeop, ".bind")
        && gssymceq(p->arguments[0], gssymopapply, gssymcodeop, ".apply")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        /* Skip type arguments */
        for (i = 2; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        if (i < p->numarguments) i++;

        pcl->size += ACE_BIND_APPLY_SIZE(p->numarguments - i);
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_cast_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymopcoerce, gssymcodeop, ".coerce")
        || gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")
        || gssymceq(p->directive, gssymoptyapp, gssymcodeop, ".tyapp")
    ) {
        /* no effect on representation */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_bytecode_size_cont_push_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    int i;

    if (gssymceq(p->directive, gssymopapp, gssymcodeop, ".app")) {
        pcl->size += ACE_APP_SIZE(p->numarguments);
    } else if (gssymceq(p->directive, gssymopforce, gssymcodeop, ".force")) {
        int creg;
        struct gsbc_code_item_type *cty;

        creg = gsbc_find_register(p, pcl->codenames, pcl->ncodes, p->arguments[0]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0])
        ;

        pcl->size += ACE_FORCE_SIZE(cty->numfvs);
    } else if (gssymceq(p->directive, gssymopstrict, gssymcodeop, ".strict")) {
        int creg;
        struct gsbc_code_item_type *cty;

        creg = gsbc_find_register(p, pcl->codenames, pcl->ncodes, p->arguments[0]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0])
        ;

        pcl->size += ACE_STRICT_SIZE(cty->numfvs);
    } else if (gssymceq(p->directive, gssymopubanalyze, gssymcodeop, ".ubanalyze")) {
        int creg;
        struct gsbc_code_item_type *cty;
        int ncases;

        for (i = 0; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        ncases = i / 2;

        if (ncases == 0)
            gsfatal("%P: Illegal .ubanalyze with no constructors; use .ubeanalyze if you need to eliminate an expression of empty un-boxed sum type", p->pos)
        ;
        creg = gsbc_find_register(p, pcl->codenames, pcl->ncodes, p->arguments[1]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1])
        ;

        pcl->size += ACE_UBANALYZE_SIZE(ncases, cty->numfvs);
    } else {
        return 0;
    }
    return 1;
}

static void gsbc_bytecode_size_case(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_bytecode_size_code_closure *);
static void gsbc_bytecode_size_default(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_bytecode_size_code_closure *);

static
int
gsbc_bytecode_size_terminal_code_op(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_bytecode_size_code_closure *pcl)
{
    int i;

    if (gssymceq((*pp)->directive, gssymopyield, gssymcodeop, ".yield")) {
        pcl->size += ACE_YIELD_SIZE();
    } else if (gssymceq((*pp)->directive, gssymopenter, gssymcodeop, ".enter")) {
        pcl->size += ACE_ENTER_SIZE();
    } else if (gssymceq((*pp)->directive, gssymopubprim, gssymcodeop, ".ubprim")) {
        struct gsregistered_primset *prims;

        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            int nargs;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++);
            if (i < (*pp)->numarguments) i++;
            nargs = (*pp)->numarguments - i;

            pcl->size += ACE_UBPRIM_SIZE(nargs);
        } else {
            pcl->size += ACE_UNKNOWN_UBPRIM_SIZE();
        }
    } else if (gssymceq((*pp)->directive, gssymoplprim, gssymcodeop, ".lprim")) {
        struct gsregistered_primset *prims;

        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            int nargs;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++);
            if (i < (*pp)->numarguments) i++;
            nargs = (*pp)->numarguments - i;

            pcl->size += ACE_LPRIM_SIZE(nargs);
        } else {
            pcl->size += ACE_UNKNOWN_LPRIM_SIZE();
        }
    } else if (gssymceq((*pp)->directive, gssymopundef, gssymcodeop, ".undef")) {
        pcl->size += ACE_UNDEF_SIZE();
    } else if (gssymceq((*pp)->directive, gssymopanalyze, gssymcodeop, ".analyze")) {
        int nconstrs;

        nconstrs = (*pp)->numarguments - 1;
        pcl->size += GS_SIZE_BYTECODE(1);
        pcl->size += nconstrs * sizeof(struct gsbc *);

        for (i = 0; i < nconstrs; i++) {
            int nregs;

            nregs = pcl->nregs;
            gsbc_bytecode_size_case(ppseg, pp, pcl);
            pcl->nregs = nregs;
        }
    } else if (gssymceq((*pp)->directive, gssymopdanalyze, gssymcodeop, ".danalyze")) {
        int nconstrs;
        int nregs;

        nconstrs = (*pp)->numarguments - 1;
        pcl->size += ACE_DANALYZE_SIZE(nconstrs);

        nregs = pcl->nregs;
        gsbc_bytecode_size_default(ppseg, pp, pcl);
        pcl->nregs = nregs;

        for (i = 0; i < nconstrs; i++) {
            nregs = pcl->nregs;
            gsbc_bytecode_size_case(ppseg, pp, pcl);
            pcl->nregs = nregs;
        }
    } else if (
        gssymceq((*pp)->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq((*pp)->arguments[0], gssymopclosure, gssymcodeop, ".closure")
    ) {
        int creg;
        struct gsbc_code_item_type *cty;

        creg = gsbc_find_register(*pp, pcl->codenames, pcl->ncodes, (*pp)->arguments[1]);
        if (!(cty = pcl->codetypes[creg]))
            gsfatal("%P: Cannot find type of %y", (*pp)->pos, (*pp)->arguments[1])
        ;

        pcl->size += ACE_BODY_CLOSURE_SIZE(cty->numfvs);
    } else if (
        gssymceq((*pp)->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq((*pp)->arguments[0], gssymopundefined, gssymcodeop, ".undefined")
    ) {
        pcl->size += ACE_BODY_UNDEFINED_SIZE();
    } else if (
        gssymceq((*pp)->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq((*pp)->arguments[0], gssymopappty, gssymcodeop, ".appty")
    ) {
        pcl->size += ACE_BODY_ALIAS_SIZE();
    } else if (
        gssymceq((*pp)->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq((*pp)->arguments[0], gssymopapply, gssymcodeop, ".apply")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", (*pp)->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        /* Skip type arguments */
        for (i = 2; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++);
        if (i < (*pp)->numarguments) i++;

        pcl->size += ACE_BODY_APPLY_SIZE((*pp)->numarguments - i);
    } else if (gssymceq((*pp)->directive, gssymopbody, gssymcodeop, ".body")) {
        gsfatal(UNIMPL("%P: gsbc_bytecode_size_item (%y)"), (*pp)->pos, (*pp)->arguments[0]);
    } else {
        return 0;
    }

    return 1;
}

static
void
gsbc_bytecode_size_case(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_bytecode_size_code_closure *pcl)
{
    *pp = gsinput_next_line(ppseg, *pp);
    if (!gssymceq((*pp)->directive, gssymopcase, gssymcodeop, ".case"))
        gsfatal("%P: Expected .case next", (*pp)->pos)
    ;
    *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_bytecode_size_cont_type_arg(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_bytecode_size_code_type_let_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_bytecode_size_cont_arg_code_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_bytecode_size_alloc_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (
        gsbc_bytecode_size_cast_op(*pp, pcl)
        || gsbc_bytecode_size_cont_push_op(*pp, pcl)
    )
        *pp = gsinput_next_line(ppseg, *pp)
    ;
    if (!gsbc_bytecode_size_terminal_code_op(ppseg, pp, pcl)) {
        gsfatal(UNIMPL("%P: gsbc_bytecode_size_case(%y)"), (*pp)->pos, (*pp)->directive);
    }
}

static
void
gsbc_bytecode_size_default(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_bytecode_size_code_closure *pcl)
{
    *pp = gsinput_next_line(ppseg, *pp);
    if (!gssymceq((*pp)->directive, gssymopdefault, gssymcodeop, ".default"))
        gsfatal("%P: Expected .default next", (*pp)->pos)
    ;
    *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_bytecode_size_alloc_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (
        gsbc_bytecode_size_cast_op(*pp, pcl)
        || gsbc_bytecode_size_cont_push_op(*pp, pcl)
    )
        *pp = gsinput_next_line(ppseg, *pp)
    ;
    if (!gsbc_bytecode_size_terminal_code_op(ppseg, pp, pcl)) {
        gsfatal(UNIMPL("%P: gsbc_bytecode_size_default(%y)"), (*pp)->pos, (*pp)->directive);
    }
}

/* §section Actual Byte Compiler */

static void gsbc_bytecompile_data_item(struct gsfile_symtable *, uint, struct gsparsedline *, gsvalue *, int, int);
static void gsbc_bytecompile_code_item(struct gsfile_symtable *, uint, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco **, int, int);

void
gsbc_bytecompile_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, struct gsbco **bcos, int n)
{
    int i;
    struct gsparsedfile_segment *pseg;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymtypelable:
            case gssymcoercionlable:
                break;
            case gssymdatalable:
                gsbc_bytecompile_data_item(symtable, items[i].file->features, items[i].v, heap, i, n);
                break;
            case gssymcodelable:
                pseg = items[i].pseg;
                gsbc_bytecompile_code_item(symtable, items[i].file->features, &pseg, items[i].v, bcos, i, n);
                break;
            default:
                gsfatal(UNIMPL("%P: gsbc_bytecompile_scc(type = %d)"), items[i].v->pos, items[i].type);
        }
    }
}

void
gsbc_bytecompile_data_item(struct gsfile_symtable *symtable, uint features, struct gsparsedline *p, gsvalue *heap, int i, int n)
{
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        struct gsrecord *record;
        struct gsrecord_fields *fields;
        int j;

        fields = (struct gsrecord_fields *)heap[i];
        record = (struct gsrecord *)fields;
        record->pos = p->pos;
        record->type = gsrecord_fields;

        for (j = 0; j < p->numarguments && p->arguments[j]->type != gssymseparator; j += 2) {
            if (j > 0 && strcmp(p->arguments[j - 2]->name, p->arguments[j]->name) > 0)
                gsfatal("%P: field %s should come after %s", p->pos, p->arguments[j - 2]->name, p->arguments[j]->name)
            ;
            if (j > 0 && strcmp(p->arguments[j - 2]->name, p->arguments[j]->name) == 0)
                gsfatal("%P: duplicated field %s", p->pos, p->arguments[j - 2]->name)
            ;
            fields->fields[j / 2] = gssymtable_get_data(symtable, p->arguments[j + 1]);
            if (!fields->fields[j / 2])
                gsfatal(UNIMPL("%P: can't find data value %y"), p->pos, p->arguments[j + 1])
            ;
        }
        fields->numfields = j / 2;
    } else if (gssymceq(p->directive, gssymconstr, gssymdatadirective, ".constr")) {
        struct gstype_sum *sum;
        struct gsconstr *constr;
        int j;

        sum = (struct gstype_sum *)gssymtable_get_type(symtable, p->arguments[0]);

        constr = (struct gsconstr *)heap[i];
        constr->pos = p->pos;
        constr->type = gsconstr_args;
        for (j = 0; j < sum->numconstrs; j++) {
            if (p->arguments[1] == sum->constrs[j].name) {
                constr->a.constrnum = j;
                goto have_constr;
            }
        }
        gsfatal("%P: Sum type %y has no constructor %y", p->pos, p->arguments[0], p->arguments[1]);
    have_constr:
        if (p->numarguments == 3) {
            constr->a.numargs = 1;
            constr->a.arguments[0] = gssymtable_get_data(symtable, p->arguments[2]);
        } else {
            constr->a.numargs = (p->numarguments - 2) / 2;
            for (j = 0; 2+j < p->numarguments; j += 2) {
                constr->a.arguments[j / 2] = gssymtable_get_data(symtable, p->arguments[2+j+1]);
            }
        }
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        ;
    } else if (gssymceq(p->directive, gssymstring, gssymdatadirective, ".string")) {
        char *eos;
        gsvalue tail, *pptail;

        eos = p->arguments[0]->name;
        tail = heap[i];
        pptail = 0;
        while (*eos) {
            struct gsconstr *cons;

            cons = (struct gsconstr *)tail;

            if (pptail)
                *pptail = tail
            ;
            cons->pos = p->pos;
            cons->type = gsconstr_args;
            cons->a.constrnum = 0;
            cons->a.numargs = 2;
            eos = gsbc_parse_rune_literal(p->pos, features, eos, &cons->a.arguments[0]);
            pptail = &cons->a.arguments[1];

            tail = (gsvalue)((uchar*)tail + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        }
        if (p->numarguments >= 2) {
            gsfatal(UNIMPL("%P: .string with provided tail"), p->pos);
        } else {
            struct gsconstr *gsnil;

            gsnil = (struct gsconstr *)tail;

            if (pptail)
                *pptail = tail
            ;

            gsnil->pos = p->pos;
            gsnil->type = gsconstr_args;
            gsnil->a.constrnum = 1;
            gsnil->a.numargs = 0;
        }
    } else if (gssymceq(p->directive, gssymlist, gssymdatadirective, ".list")) {
        gsvalue tail, *pptail;
        int j;

        tail = heap[i];
        pptail = 0;
        for (j = 1; j < p->numarguments && p->arguments[j]->type != gssymseparator; j++) {
            struct gsconstr *cons;

            cons = (struct gsconstr *)tail;
            if (pptail)
                *pptail = tail
            ;

            cons->pos = p->pos;
            cons->type = gsconstr_args;
            cons->a.constrnum = 0;
            cons->a.numargs = 2;
            cons->a.arguments[0] = gssymtable_get_data(symtable, p->arguments[j]);
            if (!cons->a.arguments[0])
                gsfatal(UNIMPL("%P: Cannot find address of data item %y"), p->pos, p->arguments[j])
            ;
            pptail = &cons->a.arguments[1];
            tail = (gsvalue)((uchar*)tail + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        }
        if (j < p->numarguments)
            gsfatal(UNIMPL("%P: Dotted .lists next"), p->pos)
        ; else {
            struct gsconstr *gsnil;

            gsnil = (struct gsconstr *)tail;

            if (pptail)
                *pptail = tail
            ;

            gsnil->pos = p->pos;
            gsnil->type = gsconstr_args;
            gsnil->a.constrnum = 1;
            gsnil->a.numargs = 0;
        }
    } else if (gssymceq(p->directive, gssymregex, gssymdatadirective, ".regex")) {
        static gsinterned_string gssymtyregex, gssymtyrune, gssymconstrsymbol, gssymconstrclass, gssymconstrnegclass, gssymconstrstar, gssymconstrsum, gssymconstrproduct, gssymconstrempty, gssymconstrnull;
        static gsinterned_string gssymtyclass, gssymconstrrange;
        struct gstype *type, *cltype;
        struct gstype_sum *sum, *clsum;
        int interp;
        char *eos;
        void *pout;
        struct gsconstr *reco, *resum;
        gsvalue term, *pfactor, *paddend;

        if (!gssymtyregex) gssymtyregex = gsintern_string(gssymtypelable, "regex.t");
        if (!gssymtyrune) gssymtyrune = gsintern_string(gssymtypelable, "rune.t");
        if (!gssymconstrsymbol) gssymconstrsymbol = gsintern_string(gssymconstrlable, "symbol");
        if (!gssymconstrclass) gssymconstrclass = gsintern_string(gssymconstrlable, "class");
        if (!gssymconstrnegclass) gssymconstrnegclass = gsintern_string(gssymconstrlable, "neg.class");
        if (!gssymconstrstar) gssymconstrstar = gsintern_string(gssymconstrlable, "star");
        if (!gssymtyclass) gssymtyclass = gsintern_string(gssymtypelable, "regex.class.t");
        if (!gssymconstrrange) gssymconstrrange = gsintern_string(gssymconstrlable, "range");
        if (!gssymconstrsum) gssymconstrsum = gsintern_string(gssymconstrlable, "sum");
        if (!gssymconstrproduct) gssymconstrproduct = gsintern_string(gssymconstrlable, "product");
        if (!gssymconstrempty) gssymconstrempty = gsintern_string(gssymconstrlable, "empty");
        if (!gssymconstrnull) gssymconstrnull = gsintern_string(gssymconstrlable, "null");

        type = gssymtable_get_type(symtable, gssymtyregex);
        if (!type || type->node != gstype_abstract) gsfatal("%P: regex.t is not an abstract type?!");
        type = gstype_get_definition(p->pos, symtable, type);
        if (!type || type->node != gstype_lambda) gsfatal("%P: regex.t does not have a type parameter?!");
        type = gstype_apply(p->pos, type, gssymtable_get_type(symtable, gssymtyrune));
        if (type->node != gstype_lift) gsfatal("%P: regex.t is not lifted?!", p->pos);
        type = ((struct gstype_lift*)type)->arg;
        if (type->node != gstype_sum) gsfatal("%P: regex.t is not a sum?!", p->pos);
        sum = (struct gstype_sum*)type;

        cltype = gssymtable_get_type(symtable, gssymtyclass);
        if (!cltype || cltype->node != gstype_abstract) gsfatal("%P: regex.class.t is not an abstract type?!");
        cltype = gstype_get_definition(p->pos, symtable, cltype);
        if (!cltype || cltype->node != gstype_lambda) gsfatal("%P: regex.class.t does not have a type parameter?!");
        cltype = gstype_apply(p->pos, cltype, gssymtable_get_type(symtable, gssymtyrune));
        if (cltype->node != gstype_lift) gsfatal("%P: regex.class.t is not lifted?!", p->pos);
        cltype = ((struct gstype_lift*)cltype)->arg;
        if (cltype->node != gstype_sum) gsfatal("%P: regex.class.t is not a sum?!", p->pos);
        clsum = (struct gstype_sum*)cltype;

        interp = 1;
        eos = p->arguments[0]->name;
        pout = (void*)heap[i];

        resum = (struct gsconstr *)pout;
        resum->pos = p->pos;
        resum->type = gsconstr_args;
        resum->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrsum);
        resum->a.numargs = 2;
        pout = (uchar*)resum + sizeof(*resum) + 2*sizeof(gsvalue);

        reco = (struct gsconstr *)pout;
        resum->a.arguments[0] = (gsvalue)reco;
        reco->pos = p->pos;
        reco->type = gsconstr_args;
        reco->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrnull);
        reco->a.numargs = 0;
        pout = (uchar*)reco + sizeof(*reco);

        term = 0;
        pfactor = &term;

        paddend = &resum->a.arguments[1];

        while (*eos) {
            if (*eos == '|') {
                resum = (struct gsconstr *)pout;
                resum->pos = p->pos;
                resum->type = gsconstr_args;
                resum->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrsum);
                resum->a.numargs = 2;
                pout = (uchar*)resum + sizeof(*resum) + 2*sizeof(gsvalue);

                if (term) {
                    resum->a.arguments[0] = term;
                } else {
                    reco = (struct gsconstr *)pout;
                    reco->pos = p->pos;
                    reco->type = gsconstr_args;
                    reco->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrempty);
                    reco->a.numargs = 0;
                    pout = (uchar*)reco + sizeof(*reco);
                    resum->a.arguments[0] = (gsvalue)reco;
                }
                term = 0;
                pfactor = &term;

                *paddend = (gsvalue)resum;

                eos++;
                if (*eos) {
                    paddend = &resum->a.arguments[1];
                } else {
                    reco = (struct gsconstr *)pout;
                    reco->pos = p->pos;
                    reco->type = gsconstr_args;
                    reco->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrempty);
                    reco->a.numargs = 0;
                    pout = (uchar*)reco + sizeof(*reco);
                    resum->a.arguments[1] = (gsvalue)reco;
                }
            } else {
                gsvalue re;

                if (*eos == '[') {
                    gsvalue *pclass;
                    struct gsconstr *cl;
                    gsvalue c0, c1;
                    int negated;

                    eos++;

                    negated = *eos == '^';
                    if (negated) eos++;

                    reco = (struct gsconstr *)pout;
                    reco->pos = p->pos;
                    reco->type = gsconstr_args;
                    reco->a.constrnum = negated
                        ? gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrnegclass)
                        : gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrclass)
                    ;
                    reco->a.numargs = 1;
                    pclass = &reco->a.arguments[0];
                    pout = (uchar*)reco + sizeof(struct gsconstr) + 1 * sizeof(gsvalue);

                    if (*eos == ']') {
                        cl = (struct gsconstr *)pout;
                        cl->pos = p->pos;
                        cl->type = gsconstr_args;
                        cl->a.constrnum = gstypes_find_constr_in_sum(cltype->pos, gssymtyclass, clsum, gssymconstrempty);
                        cl->a.numargs = 0;
                        pout = (uchar*)cl + sizeof(struct gsconstr);
                        *pclass = (gsvalue)cl;
                    } else while (*eos != ']') {
                        if (!*eos) gsfatal(UNIMPL("%P: Un-expected end of string in character class"), p->pos);
                        eos = gsbc_parse_rune_literal(p->pos, features, eos, &c0);
                        if (!*eos) {
                            gsfatal(UNIMPL("%P: Un-expected end of string in character class"), p->pos);
                            return;
                        } else if (*eos == '-') {
                            eos++;
                            if (!*eos) gsfatal(UNIMPL("%P: Un-expected end of string in character class"), p->pos);
                            else if (*eos == ']') gsfatal(UNIMPL("%P: Un-expected end of class in character class"), p->pos);
                            eos = gsbc_parse_rune_literal(p->pos, features, eos, &c1);

                            cl = (struct gsconstr *)pout;
                            cl->pos = p->pos;
                            cl->type = gsconstr_args;
                            cl->a.constrnum = gstypes_find_constr_in_sum(cltype->pos, gssymtyclass, clsum, gssymconstrrange);
                            cl->a.numargs = 2;
                            cl->a.arguments[0] = c0;
                            cl->a.arguments[1] = c1;
                            pout = (uchar*)cl + sizeof(struct gsconstr) + 2 * sizeof(gsvalue);
                        } else {
                            cl = (struct gsconstr *)pout;
                            cl->pos = p->pos;
                            cl->type = gsconstr_args;
                            cl->a.constrnum = gstypes_find_constr_in_sum(cltype->pos, gssymtyclass, clsum, gssymconstrsymbol);
                            cl->a.numargs = 1;
                            cl->a.arguments[0] = c0;
                            pout = (uchar*)cl + sizeof(struct gsconstr) + sizeof(gsvalue);
                        }
                        if (*eos != ']') {
                            struct gsconstr *sum;

                            sum = (struct gsconstr *)pout;
                            sum->pos = p->pos;
                            sum->type = gsconstr_args;
                            sum->a.constrnum = gstypes_find_constr_in_sum(cltype->pos, gssymtyclass, clsum, gssymconstrsum);
                            sum->a.numargs = 2;
                            sum->a.arguments[0] = (gsvalue)cl;
                            *pclass = (gsvalue)sum;
                            pclass = &sum->a.arguments[1];
                            pout = (uchar*)sum + sizeof(*sum) + 2*sizeof(gsvalue);
                        } else {
                            *pclass = (gsvalue)cl;
                        }
                    }
                    eos++;
                    re = (gsvalue)reco;
                } else if (eos[0] == '\xc2' && eos[1] == '\xa7') { /* \§ */
                    eos += 2;
                    if (interp >= p->numarguments) gsfatal("%P: More interpolation locations than values", p->pos);
                    re = gssymtable_get_data(symtable, p->arguments[interp]);
                    interp++;
                } else {
                    gsvalue c;

                    eos = gsbc_parse_rune_literal(p->pos, features, eos, &c);
                    reco = (struct gsconstr *)pout;
                    reco->pos = p->pos;
                    reco->type = gsconstr_args;
                    reco->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrsymbol);
                    reco->a.numargs = 1;
                    reco->a.arguments[0] = c;
                    pout = (uchar*)reco + sizeof(struct gsconstr) + 1 * sizeof(gsvalue);
                    re = (gsvalue)reco;
                }

                if (*eos == '*') {
                    struct gsconstr *st;

                    st = (struct gsconstr *)pout;
                    st->pos = p->pos;
                    st->type = gsconstr_args;
                    st->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrstar);
                    st->a.numargs = 1;
                    st->a.arguments[0] = re;
                    pout = (uchar*)st + sizeof(*st) + sizeof(gsvalue);

                    eos++;

                    re = (gsvalue)st;
                }

                if (*eos && *eos != '|') {
                    struct gsconstr *pr;

                    pr = (struct gsconstr *)pout;
                    pr->pos = p->pos;
                    pr->type = gsconstr_args;
                    pr->a.constrnum = gstypes_find_constr_in_sum(type->pos, gssymtyregex, sum, gssymconstrproduct);
                    pr->a.numargs = 2;
                    pr->a.arguments[0] = re;
                    *pfactor = (gsvalue)pr;
                    pfactor = &pr->a.arguments[1];
                    pout = (uchar*)pr + sizeof(*pr) + 2*sizeof(gsvalue);
                } else {
                    *pfactor = (gsvalue)re;
                }
            }
        }
        if (term) *paddend = term;

        if (interp < p->numarguments) gsfatal("%P: More interpolation values than locations", p->pos);
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gserror *er;

        er = (struct gserror *)heap[i];
        er->pos = p->pos;
        er->type = gserror_undefined;
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        struct gsheap_item *hp;
        struct gsclosure *cl;

        hp = (struct gsheap_item *)heap[i];
        memset(&hp->lock, 0, sizeof(hp->lock));
        hp->pos = p->pos;
        hp->type = gsclosure;
        cl = (struct gsclosure *)hp;
        gsargcheck(p, 0, "Code label");
        cl->cl.code = gssymtable_get_code(symtable, p->arguments[0]);
        cl->cl.numfvs = 0;
    } else if (
        gssymceq(p->directive, gssymalias, gssymdatadirective, ".alias")
        || gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")
    ) {
        ;
    } else {
        gsfatal(UNIMPL("%P: Data directive %y"), p->pos, p->directive);
    }
}

static void gsbc_byte_compile_code_ops(struct gsfile_symtable *, uint, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco *);
static void gsbc_byte_compile_api_ops(struct gsfile_symtable *, uint, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco *);

void
gsbc_bytecompile_code_item(struct gsfile_symtable *symtable, uint features, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco **bcos, int i, int n)
{
    static gsinterned_string gssymexpr, gssymforcecont, gssymstrictcont, gssymubcasecont, gssymimpprog;

    if (gssymceq(p->directive, gssymexpr, gssymcodedirective, ".expr")) {
        bcos[i]->tag = gsbc_expr;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, features, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymceq(p->directive, gssymforcecont, gssymcodedirective, ".forcecont")) {
        bcos[i]->tag = gsbc_forcecont;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, features, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymceq(p->directive, gssymstrictcont, gssymcodedirective, ".strictcont")) {
        bcos[i]->tag = gsbc_strictcont;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, features, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymceq(p->directive, gssymubcasecont, gssymcodedirective, ".ubcasecont")) {
        bcos[i]->tag = gsbc_ubcasecont;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, features, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymceq(p->directive, gssymimpprog, gssymcodedirective, ".impprog")) {
        bcos[i]->tag = gsbc_impprog;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_api_ops(symtable, features, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else {
        gsfatal(UNIMPL("%P: code directive %y"), p->pos, p->directive);
    }
}

struct gsbc_byte_compile_code_or_api_op_closure {
    uint features;

    void *pout;

    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    struct gstype *regtypes[MAX_NUM_REGISTERS];

    int ntyregs;
    gsinterned_string tyregnames[MAX_NUM_REGISTERS];
    struct gstype *tyregs[MAX_NUM_REGISTERS];

    int nsubexprs;
    gsinterned_string subexprs[MAX_NUM_REGISTERS];
    struct gsbc_code_item_type *subexpr_types[MAX_NUM_REGISTERS];

    int nglobals;

    int nfvs;

    int nargs;

    int nfields;
    gsinterned_string fields[MAX_NUM_REGISTERS];
};

static void gsbc_byte_compile_code_or_api_op_closure_init(struct gsbco *, struct gsbc_byte_compile_code_or_api_op_closure *, uint);

static int gsbc_byte_compile_type_gvar_code_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_type_fv_code_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_type_arg_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_type_let_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_subcode_code_op(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_coercion_gvar_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_data_gvar_code_op(struct gsfile_symtable *, uint, struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_data_fv_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_arg_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_alloc_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_bind_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_cast_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_cont_push_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_terminal_code_op(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_byte_compile_code_or_api_op_closure *);

#define SETUP_PCODE(op) \
    do { \
        pcode = (struct gsbc *)pcl->pout; \
        pcode->pos = p->pos; \
        pcode->instr = op; \
    } while (0)

void
gsbc_byte_compile_code_ops(struct gsfile_symtable *symtable, uint features, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    struct gsbc_byte_compile_code_or_api_op_closure cl;

    gsbc_byte_compile_code_or_api_op_closure_init(pbco, &cl, features);

    /* §paragraph{Global Variables} */
    while (gsbc_byte_compile_type_gvar_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_subcode_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_coercion_gvar_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_data_gvar_code_op(symtable, features, p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Free Variables} */
    while (gsbc_byte_compile_type_fv_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_type_let_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_data_fv_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);

    /* §paragraph{Arguments} */
    while (
        gsbc_byte_compile_cast_op(p, &cl)
        || gsbc_byte_compile_type_arg_code_op(p, &cl)
        || gsbc_byte_compile_type_let_code_op(p, &cl)
        || gsbc_byte_compile_arg_code_op(p, &cl)
    )
        p = gsinput_next_line(ppseg, p)
    ;

    /* §paragraph{Expression} */
    while (gsbc_byte_compile_alloc_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (
        gsbc_byte_compile_cast_op(p, &cl)
        || gsbc_byte_compile_cont_push_op(p, &cl)
    )
        p = gsinput_next_line(ppseg, p)
    ;
    if (!gsbc_byte_compile_terminal_code_op(ppseg, &p, &cl)) {
        gsfatal(UNIMPL("%P: Code op %s"), p->pos, p->directive->name);
    }

    pbco->numsubexprs = cl.nsubexprs;
    pbco->numglobals = cl.nglobals;
    pbco->numfvs = cl.nfvs;
    pbco->numargs = cl.nargs;
}

void
gsbc_byte_compile_code_or_api_op_closure_init(struct gsbco *pbco, struct gsbc_byte_compile_code_or_api_op_closure *pcl, uint features)
{
    pcl->features = features;
    pcl->ntyregs = pcl->nregs = pcl->nsubexprs = pcl->nglobals = pcl->nfvs = pcl->nargs = pcl->nfields = 0;
    pcl->pout = (uchar*)pbco + sizeof(struct gsbco);
    memset(pcl->regtypes, 0, sizeof(pcl->regtypes));
    memset(pcl->subexpr_types, 0, sizeof(pcl->subexpr_types));
}

static
int
gsbc_byte_compile_type_fv_code_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (gssymceq(p->directive, gssymoptyfv, gssymcodeop, ".tyfv")) {
        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;
        pcl->tyregnames[pcl->ntyregs] = p->label;
        pcl->tyregs[pcl->ntyregs] = gstypes_compile_type_var(p->pos, p->label, gskind_compile(p->pos, p->arguments[0]));
        pcl->ntyregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_type_gvar_code_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymoptygvar, gssymcodeop, ".tygvar")
        || gssymceq(p->directive, gssymoptyextabstype, gssymcodeop, ".tyextabstype")
    ) {
        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;
        pcl->tyregnames[pcl->ntyregs] = p->label;
        pcl->tyregs[pcl->ntyregs] = gssymtable_get_type(symtable, p->label);
        pcl->ntyregs++;
    } else if (gssymceq(p->directive, gssymoptyextelimprim, gssymcodeop, ".tyextelimprim")) {
        /* TODO: is this too much duplication w/ §ccode{gsbc_typecheck_code_type_gvar_op}? */
        struct gsregistered_primset *prims;
        struct gskind *kind;
        enum gsprim_type_group group;

        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;

        prims = gsprims_lookup_prim_set(p->arguments[0]->name);

        if (p->directive == 0 && !prims) /* > gssymtydefinedprim */
            gsfatal("%P: Unknown primset %y, which is bad because it supposedly contains defined primtype %y", p->pos, p->arguments[0], p->arguments[1])
        ;

        if (p->directive == 0) /* > gssymtydefinedprim */
            group = gsprim_type_defined
        ; else if (p->directive == 0) /* > gssymtyintrprim */
            group = gsprim_type_intr
        ; else if (p->directive == gssymoptyextelimprim)
            group = gsprim_type_elim
        ; else if (p->directive == 0)
            group = gsprim_type_imp
        ; else {
            group = -1;
            gsfatal(UNIMPL("%P: Get group for %y"), p->pos, p->directive);
        }
        kind = gskind_compile(p->pos, p->arguments[2]);
        pcl->tyregnames[pcl->ntyregs] = p->label;
        pcl->tyregs[pcl->ntyregs] = prims
            ? gstypes_compile_knprim(p->pos, group, prims, p->arguments[1], kind)
            : gstypes_compile_unprim(p->pos, group, p->arguments[0], p->arguments[1], kind)
        ;
        pcl->ntyregs++;
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_byte_compile_type_let_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    int i;

    if (gssymceq(p->directive, gssymoptylet, gssymcodeop, ".tylet")) {
        int reg;
        struct gstype *type;

        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;
        pcl->tyregnames[pcl->ntyregs] = p->label;
        reg = gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[0]);
        type = pcl->tyregs[reg];
        for (i = 1; i < p->numarguments; i++) {
            int regarg;
            regarg = gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i]);
            type = gstype_apply(p->pos, type, pcl->tyregs[regarg]);
        }
        pcl->tyregs[pcl->ntyregs] = type;
        pcl->ntyregs++;
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_byte_compile_type_arg_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (gssymceq(p->directive, gssymoptyarg, gssymcodeop, ".tyarg")) {
        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;
        pcl->tyregnames[pcl->ntyregs] = p->label;
        pcl->tyregs[pcl->ntyregs] = gstypes_compile_type_var(p->pos, p->label, gskind_compile(p->pos, p->arguments[0]));
        pcl->ntyregs++;
    } else if (gssymceq(p->directive, gssymopexkarg, gssymcodeop, ".exkarg")) {
        if (pcl->ntyregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many type registers", p->pos)
        ;
        pcl->tyregnames[pcl->ntyregs] = p->label;
        pcl->tyregs[pcl->ntyregs] = gstypes_compile_type_var(p->pos, p->label, gskind_compile(p->pos, p->arguments[0]));
        pcl->ntyregs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_subcode_code_op(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (gssymceq(p->directive, gssymopsubcode, gssymcodeop, ".subcode")) {
        struct gsbco **psubcode;

        if (pcl->nsubexprs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many sub-expressions; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->subexprs[pcl->nsubexprs] = p->label;
        pcl->subexpr_types[pcl->nsubexprs] = gssymtable_get_code_type(symtable, p->label);
        psubcode = (struct gsbco **)pcl->pout;
        *psubcode++ = gssymtable_get_code(symtable, p->label);
        pcl->pout = (uchar*)psubcode;
        pcl->nsubexprs++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_coercion_gvar_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (gssymceq(p->directive, gssymopcogvar, gssymcodeop, ".cogvar")) {
        /* Type erasure */
    } else {
        return 0;
    }
    return 1;
}

#define ADD_LABEL_TO_REGS_WITH_TYPE(ty) \
    do { \
        if (pcl->nregs >= MAX_NUM_REGISTERS) \
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS) \
        ; \
        pcl->regs[pcl->nregs] = p->label; \
        pcl->regtypes[pcl->nregs] = (ty); \
        pcl->nregs++; \
    } while (0)

static gsvalue gsbc_parse_natural_literal(struct gspos, char *);

int
gsbc_byte_compile_data_gvar_code_op(struct gsfile_symtable *symtable, uint features, struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (gssymceq(p->directive, gssymopgvar, gssymcodeop, ".gvar")) {
        gsvalue *pglobal;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pglobal = (gsvalue *)pcl->pout;
        *pglobal = gssymtable_get_data(symtable, p->label);
        pcl->pout = pglobal + 1;
        pcl->nglobals++;
        ADD_LABEL_TO_REGS_WITH_TYPE(gssymtable_get_data_type(symtable, p->label));
    } else if (gssymceq(p->directive, gssymoprune, gssymcodeop, ".rune")) {
        gsvalue *pglobal;
        char *eos;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->regs[pcl->nregs] = p->label;

        pglobal = (gsvalue *)pcl->pout;
        eos = gsbc_parse_rune_literal(p->pos, features, p->arguments[0]->name, pglobal);
        if (*eos)
            gsfatal("%P: %y: More than one rune in argument to .rune", p->pos, p->arguments[0])
        ;
        pcl->pout = pglobal + 1;

        pcl->nregs++;
        pcl->nglobals++;
    } else if (gssymceq(p->directive, gssymopnatural, gssymcodeop, ".natural")) {
        gsvalue *pglobal;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->regs[pcl->nregs] = p->label;

        pglobal = (gsvalue *)pcl->pout;
        *pglobal = gsbc_parse_natural_literal(p->pos, p->arguments[0]->name);
        pcl->pout = pglobal + 1;

        pcl->nregs++;
        pcl->nglobals++;
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_data_fv_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    int i;

    if (
        gssymceq(p->directive, gssymopfv, gssymcodeop, ".fv")
        || gssymceq(p->directive, gssymopefv, gssymcodeop, ".efv")
    ) {
        struct gstype *type;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        if (p->directive == gssymopefv) {
            SETUP_PCODE(gsbc_op_efv);
            ACE_EFV_REGNUM(pcode) = pcl->nregs;
            pcl->pout = ACE_EFV_SKIP(pcode);
        }

        type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *argtype;

            argtype = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            type = gstype_apply(p->pos, type, argtype);
        }
        ADD_LABEL_TO_REGS_WITH_TYPE(type);
        pcl->nfvs++;
    } else {
        return 0;
    }
    return 1;
}

static
gsvalue
gsbc_parse_natural_literal(struct gspos pos, char *s)
{
    gsvalue n;

    n = 0;
    while (*s) {
        n *= 10;
        n += *s ++ - '0';
    }

    n |= GS_MAX_PTR;
    return n;
}

static
int
gsbc_byte_compile_arg_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    int i;

    if (
        gssymceq(p->directive, gssymoparg, gssymcodeop, ".arg")
        || gssymceq(p->directive, gssymoplarg, gssymcodeop, ".larg")
        || gssymceq(p->directive, gssymopkarg, gssymcodeop, ".karg")
        || gssymceq(p->directive, gssymopfkarg, gssymcodeop, ".fkarg")
    ) {
        struct gstype *type;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        i = p->directive == gssymopfkarg ? 1 : 0;
        type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
        for (i++; i < p->numarguments; i++) {
            struct gstype *argtype;

            argtype = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            type = gstype_apply(p->pos, type, argtype);
        }
        ADD_LABEL_TO_REGS_WITH_TYPE(type);

        pcl->nargs++;
        if (p->directive == gssymopfkarg) {
            if (pcl->nfields > 0) {
                if (pcl->fields[pcl->nfields - 1] == p->arguments[0])
                    gsfatal("%P: Duplicate field name %y", p->pos, p->arguments[0])
                ;
                if (strcmp(pcl->fields[pcl->nfields - 1]->name, p->arguments[0]->name) > 0)
                    gsfatal("%P: Field %y should come after %y", p->pos, pcl->fields[pcl->nfields - 1], p->arguments[0])
                ;
            }
            pcl->fields[pcl->nfields] = p->arguments[0];
            pcl->nfields++;
        }
    } else {
        return 0;
    }

    return 1;
}

static int gsbc_find_field_in_product(struct gstype_product *, gsinterned_string);

#define ADD_LABEL_TO_REGS() \
    do { \
        if (pcl->nregs >= MAX_NUM_REGISTERS) \
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS) \
        ; \
        pcl->regs[pcl->nregs++] = p->label; \
    } while (0)

int
gsbc_byte_compile_alloc_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    int i, j;

    if (gssymceq(p->directive, gssymopclosure, gssymcodeop, ".closure")) {
        int creg = 0;
        struct gsbc_code_item_type *ctype;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        pcode = (struct gsbc *)pcl->pout;

        creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[0]);
        ctype = pcl->subexpr_types[creg];
        if (!ctype) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0]);
        ctype = gsbc_typecheck_copy_code_item_type(ctype);

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_closure;
        ACE_CLOSURE_CODE(pcode) = (uchar)creg;

        /* §paragraph{Calculate type of the bound variable} */
        for (i = 1; i < p->numarguments; i++) {
            struct gstype *type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            for (j = i - 1 + 1; j < ctype->numftyvs; j++)
                if (gstypes_is_ftyvar(ctype->tyfvs[j], type))
                    gsfatal("%P: %y has a free type variable %y which is free in the binding for %y; please don't do that", p->pos, p->arguments[0], ctype->tyfvs[j], p->arguments[i])
            ;
            ctype->result_type = gstypes_subst(p->pos, ctype->result_type, ctype->tyfvs[i - 1], type);
        }

        ACE_CLOSURE_NUMFVS(pcode) = (uchar)ctype->numfvs;
        for (i = 0; i < ctype->numfvs; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, ctype->fvs[i]);
            ACE_CLOSURE_FV(pcode, i) = (uchar)regarg; /* > pcode->args[2 + i] */
        }

        ADD_LABEL_TO_REGS_WITH_TYPE(ctype->result_type);
        pcl->pout = ACE_CLOSURE_SKIP(pcode);
    } else if (gssymceq(p->directive, gssymopprim, gssymcodeop, ".prim")) {
        struct gsregistered_primset *prims;

        ADD_LABEL_TO_REGS();

        SETUP_PCODE(0);
        if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
            int nargs, first_arg;
            struct gsregistered_prim *prim;

            prim = gsprims_lookup_prim(prims, p->arguments[1]->name);

            pcode->instr = gsbc_op_prim;
            ACE_PRIM_PRIMSET_INDEX(pcode) = gsprims_prim_set_index(prims);
            ACE_PRIM_INDEX(pcode) = prim->index;

            /* §paragraph{Skipping free type variables} */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;

            nargs = p->numarguments - i;
            first_arg = i;
            ACE_PRIM_NARGS(pcode) = nargs;
            for (i = first_arg; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
                ACE_PRIM_ARG(pcode, i - first_arg) = regarg;
            }

            pcl->pout = ACE_PRIM_SKIP(pcode);
        } else {
            pcode->instr = gsbc_op_unknown_prim;
            pcl->pout = ACE_UNKNOWN_PRIM_SKIP(pcode);
        }
    } else if (gssymceq(p->directive, gssymopimpprim, gssymcodeop, ".impprim")) {
        struct gsregistered_primset *prims;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->regs[pcl->nregs] = p->label;

        if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
            int nargs, first_arg;
            struct gsregistered_prim *prim;

            prim = gsprims_lookup_prim(prims, p->arguments[2]->name);

            SETUP_PCODE(gsbc_op_api_prim);
            ACE_IMPPRIM_INDEX(pcode) = prim->index;

            /* §paragraph{Skipping type arguments} */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;

            nargs = p->numarguments - i;
            first_arg = i;
            ACE_IMPPRIM_NUMARGS(pcode) = (uchar)nargs;
            for (i = first_arg; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
                ACE_IMPPRIM_ARG(pcode, i - first_arg) = (uchar)regarg;
            }

            pcl->pout = ACE_IMPPRIM_SKIP(pcode);
        } else {
            SETUP_PCODE(gsbc_op_unknown_api_prim);
            pcl->pout = GS_NEXT_BYTECODE(pcode, 0);
        }
        pcl->nregs++;
    } else if (
        gssymceq(p->directive, gssymopconstr, gssymcodeop, ".constr")
        || gssymceq(p->directive, gssymopexconstr, gssymcodeop, ".exconstr")
    ) {
        struct gstype *type;
        struct gstype_sum *sum;
        int first_val_arg;

        type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[0])];
        sum = (struct gstype_sum *)type;

        ADD_LABEL_TO_REGS_WITH_TYPE(type);

        SETUP_PCODE(gsbc_op_constr);

        for (i = 0; i < sum->numconstrs; i++) {
            if (p->arguments[1] == sum->constrs[i].name) {
                ACE_CONSTR_CONSTRNUM(pcode) = i;
                goto have_constr;
            }
        }
        gsfatal("%P: Sum type %y has no constructor %y", p->pos, p->arguments[0], p->arguments[1]);
    have_constr:

        first_val_arg = 2;
        if (p->directive == gssymopexconstr) {
            /* Skip type arguments */
            for (i = first_val_arg; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            first_val_arg = i;
        }
        if (p->numarguments == first_val_arg + 1) {
            ACE_CONSTR_NUMARGS(pcode) = 1;
            ACE_CONSTR_ARG(pcode, 0) = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[2]);
        } else {
            ACE_CONSTR_NUMARGS(pcode) = (p->numarguments - first_val_arg) / 2;
            for (i = 0; first_val_arg+i < p->numarguments; i += 2) {
                ACE_CONSTR_ARG(pcode, i / 2) = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[first_val_arg+i+1]);
            }
        }
        pcl->pout = ACE_CONSTR_SKIP(pcode);
    } else if (
        gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")
        || gssymceq(p->directive, gssymoplrecord, gssymcodeop, ".lrecord")
    ) {
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->regs[pcl->nregs] = p->label;

        SETUP_PCODE(gsbc_op_record);

        for (i = 0; i < p->numarguments && p->arguments[i]->type != gssymseparator; i += 2) {
            int reg;

            if (i > 0) {
                if (p->arguments[i] == p->arguments[i - 2])
                    gsfatal("%P: Duplicate field %y", p->pos, p->arguments[i])
                ;
                if (strcmp(p->arguments[i]->name, p->arguments[i - 2]->name) < 0)
                    gsfatal("%P: Field %y should come before %y", p->pos, p->arguments[i], p->arguments[i - 2])
                ;
            }
            reg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i + 1]);
            ACE_RECORD_FIELD(pcode, i / 2) = reg;
        }
        ACE_RECORD_NUMFIELDS(pcode) = i / 2;
        pcl->pout = ACE_RECORD_SKIP(pcode);
        pcl->nregs++;
    } else if (gssymceq(p->directive, gssymopfield, gssymcodeop, ".field")) {
        struct gstype *type;
        struct gstype_product *product;
        int regarg, fieldnum;

        SETUP_PCODE(gsbc_op_field);

        regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1]);
        ACE_FIELD_RECORD(pcode) = regarg;

        type = pcl->regtypes[regarg];
        if (!type) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1]);
        product = (struct gstype_product *)type;
        fieldnum = gsbc_find_field_in_product(product, p->arguments[0]);
        if (fieldnum < 0) gsfatal("%P: Type of %y has no field %y", p->pos, p->arguments[1], p->arguments[0]);

        ACE_FIELD_FIELD(pcode) = fieldnum;

        pcl->pout = ACE_FIELD_SKIP(pcode);

        ADD_LABEL_TO_REGS_WITH_TYPE(product->fields[fieldnum].type);
    } else if (gssymceq(p->directive, gssymoplfield, gssymcodeop, ".lfield")) {
        struct gstype *type;
        struct gstype_product *product;
        int regarg, fieldnum;

        SETUP_PCODE(gsbc_op_lfield);

        regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1]);
        ACE_LFIELD_RECORD(pcode) = regarg;

        type = pcl->regtypes[regarg];
        if (!type) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1]);

        product = (struct gstype_product *)((struct gstype_lift *)type)->arg;

        fieldnum = gsbc_find_field_in_product(product, p->arguments[0]);
        if (fieldnum < 0) gsfatal("%P: Type of %y has no field %y", p->pos, p->arguments[1], p->arguments[0]);

        ACE_LFIELD_FIELD(pcode) = fieldnum;

        pcl->pout = ACE_LFIELD_SKIP(pcode);

        ADD_LABEL_TO_REGS_WITH_TYPE(product->fields[fieldnum].type);
    } else if (gssymceq(p->directive, gssymopundefined, gssymcodeop, ".undefined")) {
        struct gstype *type, *tyarg;

        SETUP_PCODE(gsbc_op_undefined);

        type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[0])];
        for (i = 1; i < p->numarguments; i++) {
            tyarg = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            type = gstype_apply(p->pos, type, tyarg);
        }

        pcl->pout = ACE_UNDEFINED_SKIP(pcode);

        ADD_LABEL_TO_REGS_WITH_TYPE(type);
    } else if (
        gssymceq(p->directive, gssymopappty, gssymcodeop, ".appty")
        || gssymceq(p->directive, gssymopcast, gssymcodeop, ".cast")
        || gssymceq(p->directive, gssymoplifted, gssymcodeop, ".lifted")
    ) {
        SETUP_PCODE(gsbc_op_alias);

        ACE_ALIAS_SOURCE(pcode) = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);
        pcl->pout = ACE_ALIAS_SKIP(pcode);

        ADD_LABEL_TO_REGS();
    } else if (gssymceq(p->directive, gssymopapply, gssymcodeop, ".apply")) {
        struct gstype *type, *tyarg;
        int first_arg;

        SETUP_PCODE(gsbc_op_apply);

        ACE_APPLY_FUN(pcode) = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[0]);

        type = pcl->regtypes[ACE_APPLY_FUN(pcode)];
        if (!type) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0]);

        for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
            tyarg = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            type = gstype_instantiate(p->pos, type, tyarg);
        }
        if (i < p->numarguments) i++;
        first_arg = i;
        ACE_APPLY_NUM_ARGS(pcode) = p->numarguments - first_arg;
        for (; i < p->numarguments; i++) {
            struct gstype_fun *fun;
            int regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);

            ACE_APPLY_ARG(pcode, i - first_arg) = regarg;
            tyarg = pcl->regtypes[regarg];
            if (type->node == gstype_lift) {
                struct gstype_lift *lift = (struct gstype_lift *)type;
                type = lift->arg;
            }
            if (type->node != gstype_fun)
                gsfatal("%P: Function not of function type", p->pos)
            ;
            fun = (struct gstype_fun *)type;
            type = fun->tyres;
        }

        pcl->pout = ACE_APPLY_SKIP(pcode);

        ADD_LABEL_TO_REGS_WITH_TYPE(type);
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_find_field_in_product(struct gstype_product *prod, gsinterned_string field)
{
    int i;

    for (i = 0; i < prod->numfields; i++) {
        if (prod->fields[i].name == field) return i;
    }

    return -1;
}

int
gsbc_byte_compile_bind_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    int i, j;

    if (
        gssymceq(p->directive, gssymopbind, gssymcodeop, ".bind")
        && gssymceq(p->arguments[0], gssymopclosure, gssymcodeop, ".closure")
    ) {
        struct gsbc *pcode;
        int creg = 0;
        struct gsbc_code_item_type *cty;
        struct gstype *ty;

        pcode = (struct gsbc *)pcl->pout;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[1]);

        if (!(cty = pcl->subexpr_types[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1])
        ;
        cty = gsbc_typecheck_copy_code_item_type(cty);

        /* §paragraph{Calculate type of the bound variable} */
        for (i = 2; i < p->numarguments; i++) {
            struct gstype *type = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            for (j = i - 1 + 1; j < cty->numftyvs; j++)
                if (gstypes_is_ftyvar(cty->tyfvs[j], type))
                    gsfatal("%P: %y has a free type variable %y which is free in the binding for %y; please don't do that", p->pos, p->arguments[1], cty->tyfvs[j], p->arguments[i])
            ;
            cty->result_type = gstypes_subst(p->pos, cty->result_type, cty->tyfvs[i - 1], type);
        }
        ty = cty->result_type;
        if (ty->node == gstype_lift) {
          struct gstype_lift *lift = (struct gstype_lift *)ty;
          ty = lift->arg;
        }
        if (ty->node == gstype_app) {
          struct gstype_app *app = (struct gstype_app *)ty;
          ty = app->arg;
        } else {
          gsfatal("%P: Type of %y is not a type application", p->pos, p->arguments[1]);
        }

        ADD_LABEL_TO_REGS_WITH_TYPE(ty);

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_bind_closure;
        ACE_BIND_CLOSURE_CODE(pcode) = (uchar)creg;

        ACE_BIND_CLOSURE_NUMFVS(pcode) = (uchar)cty->numfvs;
        for (i = 0; i < cty->numfvs; i++)
            ACE_BIND_CLOSURE_FV(pcode, i) = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i])
        ;

        pcode = ACE_BIND_CLOSURE_SKIP(pcode);
        pcl->pout = (uchar *)pcode;
    } else if (
        gssymceq(p->directive, gssymopbind, gssymcodeop, ".bind")
        && gssymceq(p->arguments[0], gssymopapply, gssymcodeop, ".apply")
    ) {
        struct gsbc *pcode;
        struct gstype *type, *tyarg;
        int first_arg;

        pcode = (struct gsbc *)pcl->pout;

        SETUP_PCODE(gsbc_op_bind_apply);

        ACE_APPLY_FUN(pcode) = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[1]);

        type = pcl->regtypes[ACE_BIND_APPLY_FUN(pcode)];
        if (!type) gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0]);

        for (i = 2; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++) {
            tyarg = pcl->tyregs[gsbc_find_register(p, pcl->tyregnames, pcl->ntyregs, p->arguments[i])];
            type = gstype_instantiate(p->pos, type, tyarg);
        }
        if (i < p->numarguments) i++;
        first_arg = i;
        ACE_BIND_APPLY_NUM_ARGS(pcode) = p->numarguments - first_arg;
        for (; i < p->numarguments; i++) {
            struct gstype_fun *fun;
            int regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);

            ACE_BIND_APPLY_ARG(pcode, i - first_arg) = regarg;
            tyarg = pcl->regtypes[regarg];
            if (type->node == gstype_lift) {
                struct gstype_lift *lift = (struct gstype_lift *)type;
                type = lift->arg;
            }
            if (type->node != gstype_fun)
                gsfatal("%P: Function not of function type", p->pos)
            ;
            fun = (struct gstype_fun *)type;
            type = fun->tyres;
        }

        ADD_LABEL_TO_REGS();

        pcl->pout = ACE_BIND_APPLY_SKIP(pcode);
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_cast_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymopcoerce, gssymcodeop, ".coerce")
        || gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")
        || gssymceq(p->directive, gssymoptyapp, gssymcodeop, ".tyapp")
    ) {
        /* no effect on representation */
    } else {
        return 0;
    }
    return 1;
}

int
gsbc_byte_compile_cont_push_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    int i;

    if (gssymceq(p->directive, gssymopapp, gssymcodeop, ".app")) {
        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = p->pos;
        pcode->instr = gsbc_op_app;
        ACE_APP_NUMARGS(pcode) = p->numarguments;
        for (i = 0; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            ACE_APP_ARG(pcode, i) = regarg;
        }
        pcl->pout = ACE_APP_SKIP(pcode);
    } else if (gssymceq(p->directive, gssymopforce, gssymcodeop, ".force")) {
        int creg = 0;
        struct gsbc_code_item_type *cty;

        SETUP_PCODE(gsbc_op_force);

        creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[0]);

        ACE_FORCE_CONT(pcode) = (uchar)creg;

        if (!(cty = pcl->subexpr_types[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0])
        ;

        ACE_FORCE_NUMFVS(pcode) = (uchar)cty->numfvs;
        for (i = 0; i < cty->numfvs; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i]);
            ACE_FORCE_FV(pcode, i) = (uchar)regarg;
        }

        pcl->pout = ACE_FORCE_SKIP(pcode);
    } else if (gssymceq(p->directive, gssymopstrict, gssymcodeop, ".strict")) {
        int creg = 0;
        struct gsbc_code_item_type *cty;

        pcode = (struct gsbc *)pcl->pout;

        creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[0]);

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_strict;
        ACE_STRICT_CONT(pcode) = creg;

        if (!(cty = pcl->subexpr_types[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[0])
        ;

        ACE_STRICT_NUMFVS(pcode) = cty->numfvs;
        for (i = 0; i < cty->numfvs; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i]);
            ACE_STRICT_FV(pcode, i) = regarg;
        }

        pcl->pout = ACE_STRICT_SKIP(pcode);
    } else if (gssymceq(p->directive, gssymopubanalyze, gssymcodeop, ".ubanalyze")) {
        struct gsbc_code_item_type *cty;

        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = p->pos;
        pcode->instr = gsbc_op_ubanalzye;

        cty = 0;
        for (i = 0; i < p->numarguments && p->arguments[i]->type != gssymseparator; i += 2) {
            int creg;

            if (i + 1 >= p->numarguments || p->arguments[i + 1]->type == gssymseparator)
                gsfatal("%P: Missing continuation for final constructor", p->pos)
            ;
            creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[i + 1]);
            if (i == 0)
                if (!(cty = pcl->subexpr_types[creg]))
                    gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[i + 1])
            ;
            ACE_UBANALYZE_CONT(pcode, i / 2) = creg;
        }
        if (i == 0)
            gsfatal("%P: Illegal .ubanalyze with no constructors; use .ubeanalyze if you need to eliminate an expression of empty un-boxed sum type", p->pos)
        ;
        ACE_UBANALYZE_NUMCONTS(pcode) = i / 2;

        for (i = 0; i < cty->numfvs; i++)
            ACE_UBANALYZE_FV(pcode, i) = gsbc_find_register(p, pcl->regs, pcl->nregs, cty->fvs[i])
        ;
        ACE_UBANALYZE_NUMFVS(pcode) = cty->numfvs;

        pcl->pout = ACE_UBANALYZE_SKIP(pcode);
    } else {
        return 0;
    }
    return 1;
}

static void gsbc_byte_compile_case(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_byte_compile_code_or_api_op_closure *);
static void gsbc_byte_compile_default(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_byte_compile_code_or_api_op_closure *);

static
int
gsbc_byte_compile_terminal_code_op(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    struct gsbc **pcases;
    struct gsparsedline *p;

    int i;

    p = *pp;
    if (gssymceq((*pp)->directive, gssymopyield, gssymcodeop, ".yield")) {
        int reg;

        SETUP_PCODE(gsbc_op_yield);

        gsargcheck(*pp, 0, "target");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        ACE_YIELD_ARG(pcode) = (uchar)reg;

        pcl->pout = ACE_YIELD_SKIP(pcode);
    } else if (gssymceq((*pp)->directive, gssymopenter, gssymcodeop, ".enter")) {
        int reg = 0;

        SETUP_PCODE(gsbc_op_enter);

        gsargcheck(*pp, 0, "target");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        ACE_ENTER_ARG(pcode) = (uchar)reg;

        pcl->pout = ACE_ENTER_SKIP(pcode);
    } else if (gssymceq((*pp)->directive, gssymopubprim, gssymcodeop, ".ubprim")) {
        struct gsregistered_primset *prims;

        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = (*pp)->pos;

        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            int nargs, first_arg;
            struct gsregistered_prim *prim;

            prim = gsprims_lookup_prim(prims, (*pp)->arguments[1]->name);

            pcode->instr = gsbc_op_ubprim;

            ACE_UBPRIM_PRIMSET_INDEX(pcode) = gsprims_prim_set_index(prims);
            ACE_UBPRIM_INDEX(pcode) = prim->index;

            /* §paragraph{Skipping free type variables} */
            for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++);
            if (i < (*pp)->numarguments) i++;

            nargs = (*pp)->numarguments - i;
            first_arg = i;
            ACE_UBPRIM_NARGS(pcode) = (uchar)nargs;
            for (i = first_arg; i < (*pp)->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
                ACE_UBPRIM_ARG(pcode, i - first_arg) = (uchar)regarg;
            }

            pcl->pout = ACE_UBPRIM_SKIP(pcode);
        } else {
            pcode->instr = gsbc_op_unknown_ubprim;
            pcl->pout = ACE_UNKNOWN_UBPRIM_SKIP(pcode);
        }
    } else if (gssymceq((*pp)->directive, gssymoplprim, gssymcodeop, ".lprim")) {
        struct gsregistered_primset *prims;

        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = (*pp)->pos;

        if (prims = gsprims_lookup_prim_set((*pp)->arguments[0]->name)) {
            int nargs, first_arg;
            struct gsregistered_prim *prim;

            prim = gsprims_lookup_prim(prims, (*pp)->arguments[1]->name);

            pcode->instr = gsbc_op_lprim;

            ACE_LPRIM_PRIMSET_INDEX(pcode) = gsprims_prim_set_index(prims);
            ACE_LPRIM_INDEX(pcode) = prim->index;

            /* §paragraph{Skipping free type variables} */
            for (i = 3; i < (*pp)->numarguments && (*pp)->arguments[i]->type != gssymseparator; i++);
            if (i < (*pp)->numarguments) i++;

            nargs = (*pp)->numarguments - i;
            first_arg = i;
            ACE_LPRIM_NARGS(pcode) = (uchar)nargs;
            for (i = first_arg; i < (*pp)->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[i]);
                ACE_LPRIM_ARG(pcode, i - first_arg) = (uchar)regarg;
            }

            pcl->pout = ACE_LPRIM_SKIP(pcode);
        } else {
            pcode->instr = gsbc_op_unknown_lprim;
            pcl->pout = ACE_UNKNOWN_LPRIM_SKIP(pcode);
        }
    } else if (gssymceq((*pp)->directive, gssymopundef, gssymcodeop, ".undef")) {
        SETUP_PCODE(gsbc_op_undef);
        pcl->pout = ACE_UNDEF_SKIP(pcode);
    } else if (gssymceq((*pp)->directive, gssymopanalyze, gssymcodeop, ".analyze")) {
        int nconstrs;
        int reg;

        nconstrs = (*pp)->numarguments - 1;

        SETUP_PCODE(gsbc_op_analyze);

        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        ACE_ANALYZE_SCRUTINEE(pcode) = reg;

        pcases = ACE_ANALYZE_CASES(pcode);

        pcl->pout = (uchar*)pcases + nconstrs * sizeof(struct gsbc *);

        for (i = 0; i < nconstrs; i++) {
            int nargs, nregs, ntyregs;

            *pp = gsinput_next_line(ppseg, *pp);
            if (!gssymceq((*pp)->directive, gssymopcase, gssymcodeop, ".case"))
                gsfatal("%P: Expecting .case next")
            ;

            pcases[i] = (struct gsbc *)pcl->pout;
            nargs = pcl->nargs;
            nregs = pcl->nregs;
            ntyregs = pcl->ntyregs;
            pcl->nfields = 0;
            gsbc_byte_compile_case(ppseg, pp, pcl);
            pcl->nargs = nargs;
            pcl->nregs = nregs;
            pcl->ntyregs = ntyregs;
        }
    } else if (gssymceq((*pp)->directive, gssymopdanalyze, gssymcodeop, ".danalyze")) {
        int nconstrs;
        int reg;
        int nargs;
        int nregs;
        int prevconstr;
        struct gstype *type;
        struct gstype_sum *sum;

        nconstrs = (*pp)->numarguments - 1;

        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        type = pcl->regtypes[reg];
        if (!type) gsfatal("%P: Cannot find type of %y", (*pp)->pos, (*pp)->arguments[0]);
        sum = (struct gstype_sum *)type;

        pcode = (struct gsbc *)pcl->pout;

        pcode->pos = (*pp)->pos;
        pcode->instr = gsbc_op_danalyze;

        ACE_DANALYZE_SCRUTINEE(pcode) = reg;
        ACE_DANALYZE_NUMCONSTRS(pcode) = (*pp)->numarguments - 1;

        pcases = ACE_DANALYZE_CASES(pcode);

        pcl->pout = (uchar*)pcases + (1 + nconstrs) * sizeof(struct gsbc *);

        *pp = gsinput_next_line(ppseg, *pp);
        if (!gssymceq((*pp)->directive, gssymopdefault, gssymcodeop, ".default"))
            gsfatal("%P: Expecting .default next")
        ;

        pcases[0] = (struct gsbc *)pcl->pout;
        nargs = pcl->nargs;
        nregs = pcl->nregs;
        pcl->nfields = 0;
        gsbc_byte_compile_default(ppseg, pp, pcl);
        pcl->nargs = nargs;
        pcl->nregs = nregs;

        prevconstr = -1;
        for (i = 0; i < nconstrs; i++) {
            *pp = gsinput_next_line(ppseg, *pp);
            if (!gssymceq((*pp)->directive, gssymopcase, gssymcodeop, ".case"))
                gsfatal("%P: Expecting .case next")
            ;

            ACE_DANALYZE_CONSTR(pcode, i) = gsbc_typecheck_find_constr((*pp)->pos, prevconstr, sum, (*pp)->arguments[0]);

            pcases[1 + i] = (struct gsbc *)pcl->pout;
            nargs = pcl->nargs;
            nregs = pcl->nregs;
            pcl->nfields = 0;
            gsbc_byte_compile_case(ppseg, pp, pcl);
            pcl->nargs = nargs;
            pcl->nregs = nregs;

            prevconstr = ACE_DANALYZE_CONSTR(pcode, i);
        }
    } else {
        return 0;
    }

    return 1;
}

static
void
gsbc_byte_compile_case(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_byte_compile_type_arg_code_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_byte_compile_type_let_code_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_byte_compile_arg_code_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_byte_compile_alloc_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (
        gsbc_byte_compile_cast_op(*pp, pcl)
        || gsbc_byte_compile_cont_push_op(*pp, pcl)
    )
        *pp = gsinput_next_line(ppseg, *pp)
    ;
    if (!gsbc_byte_compile_terminal_code_op(ppseg, pp, pcl)) {
        gsfatal(UNIMPL("%P: Un-implemented .case code op %y"), (*pp)->pos, (*pp)->directive);
    }
}

static
void
gsbc_byte_compile_default(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    *pp = gsinput_next_line(ppseg, *pp);
    while (gsbc_byte_compile_alloc_op(*pp, pcl)) *pp = gsinput_next_line(ppseg, *pp);
    while (
        gsbc_byte_compile_cast_op(*pp, pcl)
        || gsbc_byte_compile_cont_push_op(*pp, pcl)
    )
        *pp = gsinput_next_line(ppseg, *pp)
    ;
    if (!gsbc_byte_compile_terminal_code_op(ppseg, pp, pcl)) {
        gsfatal(UNIMPL("%P: Un-implemented .default code op %y"), (*pp)->pos, (*pp)->directive);
    }
}

void
gsbc_byte_compile_api_ops(struct gsfile_symtable *symtable, uint features, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    struct gsbc_byte_compile_code_or_api_op_closure cl;
    int i;
    struct gsbc *pcode;

    gsbc_byte_compile_code_or_api_op_closure_init(pbco, &cl, features);
    while (gsbc_byte_compile_type_gvar_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_subcode_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_coercion_gvar_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_data_gvar_code_op(symtable, features, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_type_fv_code_op(symtable, p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_type_let_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_type_arg_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);
    while (gsbc_byte_compile_data_fv_code_op(p, &cl)) p = gsinput_next_line(ppseg, p);

    while (
        gsbc_byte_compile_cast_op(p, &cl)
        || gsbc_byte_compile_type_let_code_op(p, &cl)
        || gsbc_byte_compile_arg_code_op(p, &cl)
    )
        p = gsinput_next_line(ppseg, p)
    ;

    while (
        gsbc_byte_compile_alloc_op(p, &cl)
        || gsbc_byte_compile_bind_op(p, &cl)
    )
        p = gsinput_next_line(ppseg, p)
    ;
    if (
        gssymceq(p->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq(p->arguments[0], gssymopclosure, gssymcodeop, ".closure")
    ) {
        int creg = 0;
        struct gsbc_code_item_type *cty;

        pcode = (struct gsbc *)cl.pout;

        creg = gsbc_find_register(p, cl.subexprs, cl.nsubexprs, p->arguments[1]);

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_body_closure;
        ACE_BODY_CLOSURE_CODE(pcode) = (uchar)creg;

        if (!(cty = cl.subexpr_types[creg]))
            gsfatal("%P: Cannot find type of %y", p->pos, p->arguments[1])
        ;
        ACE_BODY_CLOSURE_NUMFVS(pcode) = (uchar)cty->numfvs;
        for (i = 0; i < cty->numfvs; i++) {
            int regarg;

            regarg = gsbc_find_register(p, cl.regs, cl.nregs, cty->fvs[i]);
            ACE_BODY_CLOSURE_FV(pcode, i) = (uchar)regarg;
        }

        pcode = ACE_BODY_CLOSURE_SKIP(pcode);
        cl.pout = (uchar *)pcode;
    } else if (
        gssymceq(p->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq(p->arguments[0], gssymopundefined, gssymcodeop, ".undefined")
    ) {
        pcode = (struct gsbc *)cl.pout;

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_body_undefined;

        cl.pout = ACE_BODY_UNDEFINED_SKIP(pcode);
    } else if (
        gssymceq(p->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq(p->arguments[0], gssymopappty, gssymcodeop, ".appty")
    ) {
        int regarg;

        pcode = (struct gsbc *)cl.pout;

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_body_alias;
        regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[1]);
        ACE_BODY_ALIAS_SOURCE(pcode) = (uchar)regarg;

        cl.pout = ACE_BODY_ALIAS_SKIP(pcode);
    } else if (
        gssymceq(p->directive, gssymopbody, gssymcodeop, ".body")
        && gssymceq(p->arguments[0], gssymopapply, gssymcodeop, ".apply")
    ) {
        struct gsbc *pcode;
        int first_arg;

        pcode = (struct gsbc *)cl.pout;

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_body_apply;

        ACE_BODY_APPLY_FUN(pcode) = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[1]);

        for (i = 2; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        if (i < p->numarguments) i++;
        first_arg = i;
        ACE_BODY_APPLY_NUM_ARGS(pcode) = p->numarguments - first_arg;
        for (; i < p->numarguments; i++) {
            int regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);

            ACE_BODY_APPLY_ARG(pcode, i - first_arg) = regarg;
        }

        cl.pout = ACE_BODY_APPLY_SKIP(pcode);
    } else if (gssymceq(p->directive, gssymopbody, gssymcodeop, ".body")) {
        gsfatal(UNIMPL("%P: API op %y\t%y"), p->pos, p->directive, p->arguments[0]);
    } else {
        gsfatal(UNIMPL("%P: API op %y"), p->pos, p->directive);
    }

    pbco->numglobals = cl.nglobals;
    pbco->numsubexprs = cl.nsubexprs;
    pbco->numargs = cl.nargs;
    pbco->numfvs = cl.nfvs;
}
