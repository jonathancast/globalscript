/* §source.file{Byte-compiler} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsregtables.h"
#include "gstypealloc.h"
#include "gsbytecompile.h"
#include "gsheap.h"
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
            gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Total size too large for section %d; 0x%x > 0x%x", s, total_size[s], BLOCK_SIZE)
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

static char *gsbc_parse_rune_literal(struct gspos, char *, gsvalue *);

static gsinterned_string gssymundefined, gssymrecord, gssymconstr, gssymrune, gssymstring, gssymclosure, gssymcast;

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
        *psize = sizeof(struct gsrecord) + (p->numarguments / 2) * sizeof(gsvalue);
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

        eos = gsbc_parse_rune_literal(p->pos, p->arguments[0]->name, &res);
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

            eos = gsbc_parse_rune_literal(p->pos, eos, &v);
            len++;
        }
        size = len * (sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        if (p->numarguments < 2)
            size += sizeof(struct gsconstr)
        ;
        *psize = size;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        *psection = gsbc_errors;
        *psize = sizeof(struct gserror);
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        *psection = gsbc_heap;
        *psize = MAX(sizeof(struct gsclosure), sizeof(struct gsindirection));
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        gsvalue res;

        res = gssymtable_get_data(symtable, p->arguments[1]);
        if (!res)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Can't find cast referent %s", p->pos, p->arguments[1]->name)
        ;
        *psection = gsbc_indir;
        *pindir = res;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_heap_size_item(%s)", item.v->pos, p->directive->name);
    }
}

static
char *
gsbc_parse_rune_literal(struct gspos pos, char *s, gsvalue *pv)
{
    int noctets;

    *pv = 0;

    if (*s == '\\') {
        switch (s[1]) {
            case 's':
                *pv = ' ';
                break;
            case 'n':
                *pv = '\n';
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_parse_rune_literal(%s)", pos, s);
        }
        s += 2;
    } else {
        if ((*s & 0x80) == 0) {
            noctets = 1;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_parse_rune_literal(%s)", pos, s);
        }

        while (noctets--) {
            *pv <<= 8;
            *pv |= *s++;
        }
    }

    *pv |= GS_MAX_PTR;

    return s;
}

static int gsbc_bytecode_size_item(struct gsbc_item item);

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
        size = items[i].type == gssymcodelable ? gsbc_bytecode_size_item(items[i]) : 0;
        total_size += size;
    }

    if (total_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_size, BLOCK_SIZE)
    ;

    base = gsreservebytecode(total_size);

    for (i = 0; i < n; i++) {
        if (items[i].type == gssymcodelable) {
            bcos[i] = (struct gsbco*)((uchar*)base + offsets[i]);
            gssymtable_set_code(symtable, items[i].v->label, bcos[i]);
        }
    }
}

struct gsbc_bytecode_size_code_closure {
    int size;

    enum {
        phtygvars,
        phtyfvs,
        phtyargs,
        phtylets,
        phcode,
        phgvars,
        phfvs,
        phargs,
        phlets,
        phbytecodes,
    } phase;

    int nregs;
};
static int gsbc_bytecode_size_arg_code_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_alloc_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_cont_push_op(struct gsparsedline *, struct gsbc_bytecode_size_code_closure *);
static int gsbc_bytecode_size_terminal_code_op(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_bytecode_size_code_closure *);

static gsinterned_string gssymoptyfv, gssymoptyarg, gssymoptylet, gssymopcogvar, gssymopgvar, gssymopfv, gssymoparg, gssymoplarg, gssymopkarg, gssymopfkarg, gssymopalloc, gssymoprecord, gssymopeprim, gssymoplift, gssymopcoerce, gssymopapp, gssymopforce, gssymopyield, gssymopundef, gssymopanalyze, gssymopcase;

static
int
gsbc_bytecode_size_item(struct gsbc_item item)
{
    struct gsbc_bytecode_size_code_closure cl;

    int i;
    struct gsparsedline *p;
    struct gsparsedfile_segment *pseg;
    int ncodes;

    cl.size = sizeof(struct gsbco);

    cl.phase = phtygvars;
    cl.nregs = ncodes = 0;
    pseg = item.pseg;
    for (p = gsinput_next_line(&pseg, item.v); ; p = gsinput_next_line(&pseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (cl.phase > phtygvars)
                gsfatal_bad_input(p, "Too late to add type global variables");
            cl.phase = phtygvars;
            /* type erasure */
        } else if (gssymceq(p->directive, gssymoptyfv, gssymcodeop, ".tyfv")) {
            if (cl.phase > phtyfvs)
                gsfatal("%P: Too late to add type arguments", p->pos)
            ;
            cl.phase = phtyfvs;
            /* type erasure */
        } else if (gssymceq(p->directive, gssymoptyarg, gssymcodeop, ".tyarg")) {
            if (cl.phase > phtyargs)
                gsfatal("%P: Too late to add type arguments", p->pos)
            ;
            cl.phase = phtyargs;
            /* type erasure */
        } else if (gssymceq(p->directive, gssymoptylet, gssymcodeop, ".tylet")) {
            if (cl.phase > phtylets)
                gsfatal("%P: Too late to add type arguments", p->pos)
            ;
            cl.phase = phtylets;
            /* type erasure */
        } else if (gssymceq(p->directive, gssymopcogvar, gssymcodeop, ".cogvar")) {
            if (cl.phase > phgvars)
                gsfatal("%P: Too late to add global variables", p->pos)
            ;
            cl.phase = phgvars;
            /* type erasure */
        } else if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
            if (cl.phase > phgvars)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            cl.phase = phgvars;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            if (cl.size % sizeof(gsvalue))
                gsfatal("%s:%d: %s:%d: File format error: we're at a .gvar generator but our location isn't gsvalue-aligned",
                    __FILE__,
                    __LINE__,
                    p->pos.file->name,
                    p->pos.lineno
                )
            ;
            cl.size += sizeof(gsvalue);
            cl.nregs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (cl.phase > phcode)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            cl.phase = phcode;
            if (ncodes >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            if (cl.size % sizeof(struct gsbco *))
                gsfatal("%s:%d: %s:%d: File format error: we're at a .subcode generator but our location isn't struct gsbco *-aligned",
                    __FILE__, __LINE__,
                    p->pos.file->name,
                    p->pos.lineno
                )
            ;
            cl.size += sizeof(struct gsbco *);
            ncodes++;
        } else if (gssymceq(p->directive, gssymopfv, gssymcodeop, ".fv")) {
            if (cl.phase > phfvs)
                gsfatal_bad_input(p, "Too late to add free variables")
            ;
            cl.phase = phfvs;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            cl.nregs++;
        } else if (gsbc_bytecode_size_arg_code_op(p, &cl)) {
        } else if (
            gssymceq(p->directive, gssymoparg, gssymcodeop, ".arg")
            || gssymceq(p->directive, gssymoplarg, gssymcodeop, ".larg")
        ) {
            if (cl.phase > phargs)
                gsfatal_bad_input(p, "Too late to add arguments")
            ;
            cl.phase = phargs;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            cl.nregs++;
        } else if (gsbc_bytecode_size_alloc_op(p, &cl)) {
        } else if (gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")) {
            if (cl.phase > phlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            cl.phase = phlets;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.nregs++;

            cl.size += GS_SIZE_BYTECODE(1 + p->numarguments / 2); /* numfields + fields */
        } else if (gssymceq(p->directive, gssymopeprim, gssymcodeop, ".eprim")) {
            struct gsregistered_primset *prims;

            if (cl.phase > phlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            cl.phase = phlets;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.nregs++;

            if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
                int nargs;

                /* Ignore free type variables & separator (type erasure) */
                for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
                if (i < p->numarguments) i++;
                nargs = p->numarguments - i;

                cl.size += GS_SIZE_BYTECODE(2 + nargs); /* API index + # args + args */
            } else {
                cl.size += GS_SIZE_BYTECODE(0);
            }
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int nfvs;

            cl.phase = phbytecodes;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            cl.nregs++;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nfvs = p->numarguments - i;

            cl.size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
        } else if (gsbc_bytecode_size_cont_push_op(p, &cl)) {
        } else if (gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")) {
            /* no effect on representation */
        } else if (gssymceq(p->directive, gssymopcoerce, gssymcodeop, ".coerce")) {
            /* no effect on representation */
        } else if (gssymceq(p->directive, gssymopforce, gssymcodeop, ".force")) {
            int nfvs;

            cl.phase = phbytecodes;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nfvs = p->numarguments - i;

            cl.size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
        } else if (gsbc_bytecode_size_terminal_code_op(&pseg, &p, &cl)) {
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            cl.size += GS_SIZE_BYTECODE(1);
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int nfvs;

            cl.phase = phbytecodes;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nfvs = p->numarguments - i;

            cl.size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_bytecode_size_item (%s)", p->pos, p->directive->name);
        }
    }
done:

    if (cl.size % sizeof(void*))
        cl.size += sizeof(void*) - cl.size % sizeof(void*)
    ;

    return cl.size;
}

static
int
gsbc_bytecode_size_arg_code_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymopkarg, gssymcodeop, ".karg")
        || gssymceq(p->directive, gssymopfkarg, gssymcodeop, ".fkarg")
    ) {
        if (pcl->phase > phargs)
            gsfatal_bad_input(p, "Too late to add arguments")
        ;
        pcl->phase = phargs;
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;
    } else {
        return 0;
    }

    return 1;
}

static
int
gsbc_bytecode_size_alloc_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    int i;

    if (gssymceq(p->directive, gssymopalloc, gssymcodeop, ".alloc")) {
        int nfvs;

        pcl->phase = phbytecodes;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;
        pcl->nregs++;

        /* Ignore free type variables & separator (type erasure) */
        for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        if (i < p->numarguments) i++;
        nfvs = p->numarguments - i;

        pcl->size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_bytecode_size_cont_push_op(struct gsparsedline *p, struct gsbc_bytecode_size_code_closure *pcl)
{
    if (gssymceq(p->directive, gssymopapp, gssymcodeop, ".app")) {
        pcl->size += GS_SIZE_BYTECODE(1 + p->numarguments); /* nargs + args */
    } else {
        return 0;
    }
    return 1;
}

static void gsbc_bytecode_size_case(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_bytecode_size_code_closure *);

static
int
gsbc_bytecode_size_terminal_code_op(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_bytecode_size_code_closure *pcl)
{
    int i;

    if (gssymceq((*pp)->directive, gssymopyield, gssymcodeop, ".yield")) {
        pcl->size += GS_SIZE_BYTECODE(1);
    } else if (gssymceq((*pp)->directive, gssymopundef, gssymcodeop, ".undef")) {
        pcl->size += GS_SIZE_BYTECODE(0);
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
    while (*pp = gsinput_next_line(ppseg, *pp)) {
        if (gsbc_bytecode_size_arg_code_op(*pp, pcl)) {
        } else if (gsbc_bytecode_size_alloc_op(*pp, pcl)) {
        } else if (gsbc_bytecode_size_cont_push_op(*pp, pcl)) {
        } else if (gsbc_bytecode_size_terminal_code_op(ppseg, pp, pcl)) {
            return;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_bytecode_size_case(%y)", (*pp)->pos, (*pp)->directive);
        }
    }
}

/* §section{Actual Byte Compiler} */

static void gsbc_bytecompile_data_item(struct gsfile_symtable *, struct gsparsedline *, gsvalue *, int, int);
static void gsbc_bytecompile_code_item(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco **, int, int);

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
                gsbc_bytecompile_data_item(symtable, items[i].v, heap, i, n);
                break;
            case gssymcodelable:
                pseg = items[i].pseg;
                gsbc_bytecompile_code_item(symtable, &pseg, items[i].v, bcos, i, n);
                break;
            default:
                gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_bytecompile_scc(type = %d)", items[i].v->pos, items[i].type);
        }
    }
}

static
void
gsbc_bytecompile_data_item(struct gsfile_symtable *symtable, struct gsparsedline *p, gsvalue *heap, int i, int n)
{
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        struct gsrecord *record;
        int j;

        record = (struct gsrecord *)heap[i];
        record->pos = p->pos;
        record->numfields = p->numarguments / 2;

        for (j = 0; j < p->numarguments; j += 2) {
            if (j > 0 && strcmp(p->arguments[j - 2]->name, p->arguments[j]->name) > 0)
                gsfatal("%P: field %s should come after %s", p->pos, p->arguments[j - 2]->name, p->arguments[j]->name)
            ;
            if (j > 0 && strcmp(p->arguments[j - 2]->name, p->arguments[j]->name) == 0)
                gsfatal("%P: duplicated field %s", p->pos, p->arguments[j - 2]->name)
            ;
            record->fields[j / 2] = gssymtable_get_data(symtable, p->arguments[j + 1]);
            if (!record->fields[j / 2])
                gsfatal_unimpl(__FILE__, __LINE__, "%P: can't find data value %s", p->pos, p->arguments[j + 1]->name)
            ;
        }
    } else if (gssymceq(p->directive, gssymconstr, gssymdatadirective, ".constr")) {
        struct gstype_sum *sum;
        struct gsconstr *constr;
        int j;

        sum = (struct gstype_sum *)gssymtable_get_type(symtable, p->arguments[0]);

        constr = (struct gsconstr *)heap[i];
        constr->pos = p->pos;
        for (j = 0; j < sum->numconstrs; j++) {
            if (p->arguments[1] == sum->constrs[j].name) {
                constr->constrnum = j;
                goto have_constr;
            }
        }
        gsfatal("%P: Sum type %y has no constructor %y", p->pos, p->arguments[0], p->arguments[1]);
    have_constr:
        if (p->numarguments == 3) {
            constr->numargs = 1;
            constr->arguments[0] = gssymtable_get_data(symtable, p->arguments[2]);
        } else {
            constr->numargs = (p->numarguments - 2) / 2;
            for (j = 0; 2+j < p->numarguments; j += 2) {
                constr->arguments[j / 2] = gssymtable_get_data(symtable, p->arguments[2+j+1]);
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
            cons->constrnum = 0;
            cons->numargs = 2;
            eos = gsbc_parse_rune_literal(p->pos, eos, &cons->arguments[0]);
            pptail = &cons->arguments[1];

            tail = (gsvalue)((uchar*)tail + sizeof(struct gsconstr) + 2 * sizeof(gsvalue));
        }
        if (p->numarguments >= 2) {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: .string with provided tail", p->pos);
        } else {
            struct gsconstr *gsnil;

            gsnil = (struct gsconstr *)tail;

            if (pptail)
                *pptail = tail
            ;

            gsnil->pos = p->pos;
            gsnil->constrnum = 1;
            gsnil->numargs = 0;
        }
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
        cl->code = gssymtable_get_code(symtable, p->arguments[0]);
        cl->numfvs = 0;
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: Data directive %s next", p->pos, p->directive->name);
    }
}

static void gsbc_byte_compile_code_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco *);
static void gsbc_byte_compile_api_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco *);

static
void
gsbc_bytecompile_code_item(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco **bcos, int i, int n)
{
    if (gssymeq(p->directive, gssymcodedirective, ".expr")) {
        bcos[i]->tag = gsbc_expr;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymeq(p->directive, gssymcodedirective, ".forcecont")) {
        bcos[i]->tag = gsbc_forcecont;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_code_ops(symtable, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else if (gssymeq(p->directive, gssymcodedirective, ".eprog")) {
        bcos[i]->tag = gsbc_eprog;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_api_ops(symtable, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: code directive %y", p->pos, p->directive);
    }
}

struct gsbc_byte_compile_code_or_api_op_closure {
    enum {
        rttygvars,
        rttyfvs,
        rttyargs,
        rttylets,
        rtsubexprs,
        rtgvars,
        rtfvs,
        rtargs,
        rtlets,
        rtops,
    } phase;

    void *pout;

    int nregs;
    gsinterned_string regs[MAX_NUM_REGISTERS];

    int nsubexprs;
    gsinterned_string subexprs[MAX_NUM_REGISTERS];

    int nargs;

    int nfields;
    gsinterned_string fields[MAX_NUM_REGISTERS];
};

static int gsbc_byte_compile_arg_code_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_alloc_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_cont_push_op(struct gsparsedline *, struct gsbc_byte_compile_code_or_api_op_closure *);
static int gsbc_byte_compile_terminal_code_op(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_byte_compile_code_or_api_op_closure *);

static
void
gsbc_byte_compile_code_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    struct gsbc_byte_compile_code_or_api_op_closure cl;
    int i;
    struct gsbco **psubcode;
    gsvalue *pglobal;
    struct gsbc *pcode;
    int nglobals, nfvs;

    cl.phase = rttygvars;
    cl.pout = ((uchar*)pbco + sizeof(struct gsbco));
    cl.nregs = cl.nsubexprs = nglobals = nfvs = cl.nargs = cl.nfields = 0;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (cl.phase > rttygvars)
                gsfatal_bad_input(p, "Too late to add type global variables")
            ;
            cl.phase = rttygvars;
        } else if (gssymceq(p->directive, gssymoptyfv, gssymcodeop, ".tyfv")) {
            if (cl.phase > rttyfvs)
                gsfatal("%P: Too late to add free type variables", p->pos)
            ;
            cl.phase = rttyfvs;
        } else if (gssymceq(p->directive, gssymoptyarg, gssymcodeop, ".tyarg")) {
            if (cl.phase > rttyargs)
                gsfatal("%P: Too late to add type arguments", p->pos)
            ;
            cl.phase = rttyargs;
        } else if (gssymceq(p->directive, gssymoptylet, gssymcodeop, ".tylet")) {
            if (cl.phase > rttylets)
                gsfatal("%P: Too late to add type lets", p->pos)
            ;
            cl.phase = rttylets;
        } else if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (cl.phase > rtsubexprs)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            cl.phase = rtsubexprs;
            if (cl.nsubexprs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.subexprs[cl.nsubexprs] = p->label;
            psubcode = (struct gsbco **)cl.pout;
            *psubcode++ = gssymtable_get_code(symtable, p->label);
            cl.pout = (uchar*)psubcode;
            cl.nsubexprs++;
        } else if (gssymceq(p->directive, gssymopcogvar, gssymcodeop, ".cogvar")) {
            if (cl.phase > rtgvars)
                gsfatal("%P: Too late to add global variables", p->pos)
            ;
            cl.phase = rtgvars;
        } else if (gssymceq(p->directive, gssymopgvar, gssymcodeop, ".gvar")) {
            if (cl.phase > rtgvars)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            cl.phase = rtgvars;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.regs[cl.nregs] = p->label;
            pglobal = (gsvalue *)cl.pout;
            *pglobal = gssymtable_get_data(symtable, p->label);
            cl.pout = pglobal + 1;
            cl.nregs++;
            nglobals++;
        } else if (gssymceq(p->directive, gssymopfv, gssymcodeop, ".fv")) {
            if (cl.phase > rtfvs)
                gsfatal_bad_input(p, "Too late to add free variables")
            ;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.phase = rtfvs;
            cl.regs[cl.nregs] = p->label;
            cl.nregs++;
            nfvs++;
        } else if (gsbc_byte_compile_arg_code_op(p, &cl)) {
        } else if (
            gssymceq(p->directive, gssymoparg, gssymcodeop, ".arg")
            || gssymceq(p->directive, gssymoplarg, gssymcodeop, ".larg")
        ) {
            if (cl.phase > rtargs)
                gsfatal_bad_input(p, "Too late to add arguments")
            ;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.phase = rtargs;
            cl.regs[cl.nregs] = p->label;
            cl.nregs++;
            cl.nargs++;
        } else if (gsbc_byte_compile_alloc_op(p, &cl)) {
        } else if (gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")) {
            if (cl.phase > rtlets)
                gsfatal_bad_input(p, "Too late to add allocations");
            cl.phase = rtlets;
            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.regs[cl.nregs] = p->label;
            pcode = (struct gsbc *)cl.pout;
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_record;
            pcode->args[0] = p->numarguments / 2;
            for (i = 0; i < p->numarguments; i += 2) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: .record fields", p->pos);
            }
            cl.pout = GS_NEXT_BYTECODE(pcode, 1 + p->numarguments / 2);
            cl.nregs++;
        } else if (gssymceq(p->directive, gssymopeprim, gssymcodeop, ".eprim")) {
            struct gsregistered_primset *prims;

            if (cl.phase > rtlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            cl.phase = rtlets;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.regs[cl.nregs] = p->label;

            pcode = (struct gsbc *)cl.pout;
            pcode->pos = p->pos;
            if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
                int nargs, first_arg;
                struct gsregistered_prim *prim;

                prim = gsprims_lookup_prim(prims, p->arguments[2]->name);

                pcode->pos = p->pos;
                pcode->instr = gsbc_op_eprim;
                pcode->args[0] = prim->index;

                /* §paragraph{Skipping free type variables} */
                for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
                if (i < p->numarguments) i++;

                nargs = p->numarguments - i;
                first_arg = i;
                pcode->args[1] = (uchar)nargs;
                for (i = first_arg; i < p->numarguments; i++) {
                    int regarg;

                    regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                    pcode->args[2 + i - first_arg] = (uchar)regarg;
                }

                cl.pout = GS_NEXT_BYTECODE(pcode, 2 + nargs);
            } else {
                pcode->instr = gsbc_op_unknown_eprim;
                cl.pout = GS_NEXT_BYTECODE(pcode, 0);
            }
            cl.nregs++;
        } else if (gsbc_byte_compile_cont_push_op(p, &cl)) {
        } else if (gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")) {
            cl.phase = rtops;
            /* no effect on representation */
        } else if (gssymceq(p->directive, gssymopcoerce, gssymcodeop, ".coerce")) {
            cl.phase = rtops;
            /* no effect on representation */
        } else if (gssymceq(p->directive, gssymopforce, gssymcodeop, ".force")) {
            int creg = 0;
            int nfvs, first_fv;

            cl.phase = rtops;

            pcode = (struct gsbc *)cl.pout;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
            ;

            cl.regs[cl.nregs] = p->label;

            cl.nregs++;

            creg = gsbc_find_register(p, cl.subexprs, cl.nsubexprs, p->arguments[0]);

            pcode->pos = p->pos;
            pcode->instr = gsbc_op_force;
            pcode->args[0] = (uchar)creg;

            /* §paragraph{Skipping free type variables} */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;

            nfvs = p->numarguments - i;
            first_fv = i;
            pcode->args[1] = (uchar)nfvs;
            for (i = first_fv; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                pcode->args[2 + (i - first_fv)] = (uchar)regarg;
            }

            cl.pout = GS_NEXT_BYTECODE(pcode, 2 + nfvs);
        } else if (gsbc_byte_compile_terminal_code_op(ppseg, &p, &cl)) {
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            int reg = 0;

            cl.phase = rtops;
            pcode = (struct gsbc *)cl.pout;
            gsargcheck(p, 0, "target");
            reg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[0]);
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_enter;
            pcode->args[0] = (uchar)reg;
            cl.pout = GS_NEXT_BYTECODE(pcode, 1);
            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Code op %s", p->pos, p->directive->name);
        }
    }

done:

    pbco->numsubexprs = cl.nsubexprs;
    pbco->numglobals = nglobals;
    pbco->numfvs = nfvs;
    pbco->numargs = cl.nargs;
}

static
int
gsbc_byte_compile_arg_code_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    if (
        gssymceq(p->directive, gssymopkarg, gssymcodeop, ".karg")
        || gssymceq(p->directive, gssymopfkarg, gssymcodeop, ".fkarg")
    ) {
        if (pcl->phase > rtargs)
            gsfatal_bad_input(p, "Too late to add arguments")
        ;
        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
        ;
        pcl->phase = rtargs;
        pcl->regs[pcl->nregs] = p->label;
        pcl->nregs++;
        if (0) /* §ags{.arg} and §ags{.larg} */
            pcl->nargs++
        ;
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

static
int
gsbc_byte_compile_alloc_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    int i;

    if (gssymceq(p->directive, gssymopalloc, gssymcodeop, ".alloc")) {
        int creg = 0;
        int nfvs, first_fv;

        if (pcl->phase > rtlets)
            gsfatal_bad_input(p, "Too late to add allocations");
        pcl->phase = rtlets;

        pcode = (struct gsbc *)pcl->pout;

        if (pcl->nregs >= MAX_NUM_REGISTERS)
            gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
        ;

        pcl->regs[pcl->nregs] = p->label;

        pcl->nregs++;

        creg = gsbc_find_register(p, pcl->subexprs, pcl->nsubexprs, p->arguments[0]);

        pcode->pos = p->pos;
        pcode->instr = gsbc_op_alloc;
        pcode->args[0] = (uchar)creg;

        /* §paragraph{Skipping free type variables} */
        for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
        if (i < p->numarguments) i++;

        nfvs = p->numarguments - i;
        first_fv = i;
        pcode->args[1] = (uchar)nfvs;
        for (i = first_fv; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            pcode->args[2 + (i - first_fv)] = (uchar)regarg;
        }

        pcl->pout = GS_NEXT_BYTECODE(pcode, 2 + nfvs);
    } else {
        return 0;
    }
    return 1;
}

static
int
gsbc_byte_compile_cont_push_op(struct gsparsedline *p, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    int i;

    if (gssymceq(p->directive, gssymopapp, gssymcodeop, ".app")) {
        pcl->phase = rtops;
        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = p->pos;
        pcode->instr = gsbc_op_app;
        pcode->args[0] = (uchar)p->numarguments;
        for (i = 0; i < p->numarguments; i++) {
            int regarg;

            regarg = gsbc_find_register(p, pcl->regs, pcl->nregs, p->arguments[i]);
            pcode->args[1 + i] = (uchar)regarg;
        }
        pcl->pout = GS_NEXT_BYTECODE(pcode, 1 + p->numarguments);
    } else {
        return 0;
    }
    return 1;
}

static void gsbc_byte_compile_case(struct gsparsedfile_segment **, struct gsparsedline **, struct gsbc_byte_compile_code_or_api_op_closure *);

static
int
gsbc_byte_compile_terminal_code_op(struct gsparsedfile_segment **ppseg, struct gsparsedline **pp, struct gsbc_byte_compile_code_or_api_op_closure *pcl)
{
    struct gsbc *pcode;
    struct gsbc **pcases;

    int i;

    if (gssymceq((*pp)->directive, gssymopyield, gssymcodeop, ".yield")) {
        int reg;

        pcl->phase = rtops;
        pcode = (struct gsbc *)pcl->pout;
        gsargcheck(*pp, 0, "target");
        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        pcode->pos = (*pp)->pos;
        pcode->instr = gsbc_op_yield;
        pcode->args[0] = (uchar)reg;
        pcl->pout = GS_NEXT_BYTECODE(pcode, 1);
    } else if (gssymceq((*pp)->directive, gssymopundef, gssymcodeop, ".undef")) {
        pcl->phase = rtops;
        pcode = (struct gsbc *)pcl->pout;
        pcode->pos = (*pp)->pos;
        pcode->instr = gsbc_op_undef;
        pcl->pout = GS_NEXT_BYTECODE(pcode, 0);
    } else if (gssymceq((*pp)->directive, gssymopanalyze, gssymcodeop, ".analyze")) {
        int nconstrs;
        int reg;

        nconstrs = (*pp)->numarguments - 1;

        pcode = (struct gsbc *)pcl->pout;

        pcode->pos = (*pp)->pos;
        pcode->instr = gsbc_op_analyze;

        reg = gsbc_find_register(*pp, pcl->regs, pcl->nregs, (*pp)->arguments[0]);
        ACE_ANALYZE_SCRUTINEE(pcode) = reg;

        pcases = ACE_ANALYZE_CASES(pcode);

        pcl->pout = (uchar*)pcases + nconstrs * sizeof(struct gsbc *);

        for (i = 0; i < nconstrs; i++) {
            int nregs;

            pcases[i] = (struct gsbc *)pcl->pout;
            nregs = pcl->nregs;
            pcl->nfields = 0;
            gsbc_byte_compile_case(ppseg, pp, pcl);
            pcl->nregs = nregs;
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
    if (!gssymceq((*pp)->directive, gssymopcase, gssymcodeop, ".case"))
        gsfatal("%P: Expecting .case next")
    ;

    pcl->phase = rtargs;
    while (*pp = gsinput_next_line(ppseg, *pp)) {
        if (gsbc_byte_compile_arg_code_op(*pp, pcl)) {
        } else if (gsbc_byte_compile_alloc_op(*pp, pcl)) {
        } else if (gsbc_byte_compile_cont_push_op(*pp, pcl)) {
        } else if (gsbc_byte_compile_terminal_code_op(ppseg, pp, pcl)) {
            return;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Un-implemented .case code op %y", (*pp)->pos, (*pp)->directive);
        }
    }
}

static
void
gsbc_byte_compile_api_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    struct gsbc_byte_compile_code_or_api_op_closure cl;
    int i;
    struct gsbco **psubcode;
    struct gsbc *pcode;
    int nglobals, nfvs, nargs;

    cl.phase = rttygvars;
    pcode = 0;
    cl.pout = (uchar*)pbco + sizeof(struct gsbco);
    cl.nregs = nglobals = cl.nsubexprs = nfvs = nargs = 0;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (cl.phase > rttygvars)
                gsfatal_bad_input(p, "Too late to add type global variables")
            ;
            cl.phase = rttygvars;
        } else if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (cl.phase > rtsubexprs)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            cl.phase = rtsubexprs;
            if (cl.nsubexprs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.subexprs[cl.nsubexprs] = p->label;
            psubcode = (struct gsbco **)cl.pout;
            *psubcode++ = gssymtable_get_code(symtable, p->label);
            cl.pout = (uchar*)psubcode;
            cl.nsubexprs++;
        } else if (
            gssymceq(p->directive, gssymoparg, gssymcodeop, ".arg")
            || gssymceq(p->directive, gssymoplarg, gssymcodeop, ".larg")
        ) {
            if (cl.phase > rtargs)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            cl.regs[cl.nregs] = p->label;
            cl.nregs++;
            nargs++;
        } else if (gsbc_byte_compile_alloc_op(p, &cl)) {
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int creg = 0;
            int nfvs, first_fv;

            cl.phase = rtlets;

            pcode = (struct gsbc *)cl.pout;

            if (cl.nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
            ;

            cl.regs[cl.nregs] = p->label;

            cl.nregs++;

            creg = gsbc_find_register(p, cl.subexprs, cl.nsubexprs, p->arguments[0]);

            pcode->pos = p->pos;
            pcode->instr = gsbc_op_bind;
            pcode->args[0] = (uchar)creg;

            /* §paragraph{Skipping free type variables} */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;

            nfvs = p->numarguments - i;
            first_fv = i;
            pcode->args[1] = (uchar)nfvs;
            for (i = first_fv; i < p->numarguments; i++) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: store free variables", p->pos);
            }

            pcode = GS_NEXT_BYTECODE(pcode, 2 + nfvs);
            cl.pout = (uchar *)pcode;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int creg = 0;
            int nfvs, first_fv;

            pcode = (struct gsbc *)cl.pout;

            creg = gsbc_find_register(p, cl.subexprs, cl.nsubexprs, p->arguments[0]);

            pcode->pos = p->pos;
            pcode->instr = gsbc_op_body;
            pcode->args[0] = (uchar)creg;

            /* §paragraph{Skipping free type variables} */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;

            nfvs = p->numarguments - i;
            first_fv = i;
            pcode->args[1] = (uchar)nfvs;
            for (i = first_fv; i < p->numarguments; i++) {
                int regarg;

                regarg = gsbc_find_register(p, cl.regs, cl.nregs, p->arguments[i]);
                pcode->args[2 + (i - first_fv)] = (uchar)regarg;
            }

            pcode = GS_NEXT_BYTECODE(pcode, 2 + nfvs);
            cl.pout = (uchar *)pcode;

            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: API op %s next", p->pos, p->directive->name);
        }
    }

done:

    pbco->numglobals = nglobals;
    pbco->numsubexprs = cl.nsubexprs;
    pbco->numargs = nargs;
    pbco->numfvs = nfvs;
}
