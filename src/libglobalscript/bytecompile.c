/* §source.file{Byte-compiler} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>

#include "gsinputfile.h"
#include "gsregtables.h"
#include "gsbytecompile.h"
#include "gsheap.h"
#include "gstopsort.h"

static uint gsbc_heap_size_item(struct gsbc_item);
static uint gsbc_error_size_item(struct gsbc_item);
static uint gsbc_record_size_item(struct gsbc_item);
static gsvalue gsbc_get_indir_item(struct gsfile_symtable *, struct gsbc_item);

void
gsbc_alloc_data_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gsbc_data_locs *plocs, int n)
{
    uint total_heap_size, heap_offsets[MAX_ITEMS_PER_SCC], heap_size[MAX_ITEMS_PER_SCC],
        total_error_size, error_offsets[MAX_ITEMS_PER_SCC], error_size[MAX_ITEMS_PER_SCC],
        total_record_size, record_offsets[MAX_ITEMS_PER_SCC], record_size[MAX_ITEMS_PER_SCC]
    ;
    int i;
    void *heap_base, *error_base, *record_base;

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    total_heap_size = total_error_size = total_record_size = 0;
    for (i = 0; i < n; i++) {
        heap_offsets[i] = total_heap_size;
        heap_size[i] = gsbc_heap_size_item(items[i]);
        total_heap_size += heap_size[i];
        if (total_heap_size % sizeof(struct gsbco*))
            total_heap_size += sizeof(struct gsbco*) - (total_heap_size % sizeof(struct gsbco*))
        ;

        error_offsets[i] = total_error_size;
        error_size[i] = gsbc_error_size_item(items[i]);
        total_error_size += error_size[i];
        if (total_error_size % sizeof(gsinterned_string))
            total_error_size += sizeof(gsinterned_string) - (total_error_size % sizeof(gsinterned_string))
        ;

        record_offsets[i] = total_record_size;
        record_size[i] = gsbc_record_size_item(items[i]);
        total_record_size += record_size[i];
        if (total_record_size % sizeof(gsinterned_string))
            total_record_size += sizeof(gsinterned_string) - (total_record_size % sizeof(gsinterned_string))
        ;

        plocs->indir[i] = gsbc_get_indir_item(symtable, items[i]);
    }

    if (total_heap_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total heap size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_heap_size, BLOCK_SIZE)
    ;
    if (total_error_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total error size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_error_size, BLOCK_SIZE)
    ;

    heap_base = gsreserveheap(total_heap_size);
    error_base = gsreserveerrors(total_error_size);
    record_base = gsreserverecords(total_record_size);

    for (i = 0; i < n; i++) {
        plocs->heap[i] = plocs->errors[i] = plocs->records[i] = 0;
        if (heap_size[i]) {
            plocs->heap[i] = (gsvalue)((uchar*)heap_base + heap_offsets[i]);
            gssymtable_set_data(symtable, items[i].v->label, plocs->heap[i]);
        } else if (error_size[i]) {
            plocs->errors[i] = (gsvalue)((uchar*)error_base + error_offsets[i]);
            gssymtable_set_data(symtable, items[i].v->label, plocs->errors[i]);
        } else if (record_size[i]) {
            plocs->records[i] = (gsvalue)((uchar*)record_base + record_offsets[i]);
            gssymtable_set_data(symtable, items[i].v->label, plocs->records[i]);
        } else if (plocs->indir[i]) {
            gssymtable_set_data(symtable, items[i].v->label, plocs->indir[i]);
        }
    }
}

static gsinterned_string gssymundefined, gssymrecord, gssymrune, gssymclosure, gssymcast;

static
uint
gsbc_heap_size_item(struct gsbc_item item)
{
    struct gsparsedline *p;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        return 0;
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        return 0;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        return 0;
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        return MAX(sizeof(struct gsclosure), sizeof(struct gsindirection));
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        return 0;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_heap_size_item(%s)", item.v->pos, p->directive->name);
    }
    return 0;
}

static
uint
gsbc_error_size_item(struct gsbc_item item)
{
    struct gsparsedline *p;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        return 0;
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        return 0;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        return sizeof(struct gserror);
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        return 0;
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        return 0;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_error_size_item(%s)", item.v->pos, p->directive->name);
    }
    return 0;
}

static
uint
gsbc_record_size_item(struct gsbc_item item)
{
    struct gsparsedline *p;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        return sizeof(struct gsrecord) + (p->numarguments / 2) * sizeof(gsvalue);
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        return 0;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        return 0;
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        return 0;
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        return 0;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_record_size_item(%s)", p->pos, p->directive->name);
    }
    return 0;
}

static char *gsbc_parse_rune_literal(struct gspos, char *, gsvalue *);

static
gsvalue
gsbc_get_indir_item(struct gsfile_symtable *symtable, struct gsbc_item item)
{
    struct gsparsedline *p;
    gsvalue res;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        return 0;
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        char *eos;

        eos = gsbc_parse_rune_literal(p->pos, p->arguments[0]->name, &res);
        if (*eos)
            gsfatal("%P: %s: More than one rune in argument to .rune", p->pos, p->arguments[0]->name)
        ;

        return res;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        return 0;
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        return 0;
    } else if (gssymceq(p->directive, gssymcast, gssymdatadirective, ".cast")) {
        res = gssymtable_get_data(symtable, p->arguments[1]);
        if (!res)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Can't find cast referent %s", p->pos, p->arguments[1]->name)
        ;
        return res;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_get_indir_item(%s)", p->pos, p->directive->name);
    }
    return 0;
}

static
char *
gsbc_parse_rune_literal(struct gspos pos, char *s, gsvalue *pv)
{
    int noctets;

    *pv = 0;

    if ((*s & 0x80) == 0) {
        noctets = 1;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_parse_rune_literal(%s)", pos, s);
    }

    while (noctets--) {
        *pv <<= 8;
        *pv |= *s++;
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

static gsinterned_string gssymopgvar, gssymoprecord, gssymopeprim, gssymoplift, gssymopyield;

static
int
gsbc_bytecode_size_item(struct gsbc_item item)
{
    int size;
    int i;
    struct gsparsedline *p;
    struct gsparsedfile_segment *pseg;
    enum {
        phtygvars,
        phgvars,
        phcode,
        phargs,
        phlets,
        phbytecodes,
    } phase;
    int nregs, ncodes;

    size = sizeof(struct gsbco);

    phase = phtygvars;
    nregs = ncodes = 0;
    pseg = item.pseg;
    for (p = gsinput_next_line(&pseg, item.v); ; p = gsinput_next_line(&pseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (phase > phtygvars)
                gsfatal_bad_input(p, "Too late to add type global variables");
            phase = phtygvars;
            /* type erasure */
        } else if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
            if (phase > phgvars)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            phase = phgvars;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            if (size % sizeof(gsvalue))
                gsfatal("%s:%d: %s:%d: File format error: we're at a .gvar generator but our location isn't gsvalue-aligned",
                    __FILE__,
                    __LINE__,
                    p->pos.file->name,
                    p->pos.lineno
                )
            ;
            size += sizeof(gsvalue);
            nregs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (phase > phcode)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            phase = phcode;
            if (ncodes >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            if (size % sizeof(struct gsbco *))
                gsfatal("%s:%d: %s:%d: Fiel format error: we're at a .subcode generator but our location isn't struct gsbco *-aligned",
                    __FILE__, __LINE__,
                    p->pos.file->name,
                    p->pos.lineno
                )
            ;
            size += sizeof(struct gsbco *);
            ncodes++;
        } else if (gssymeq(p->directive, gssymcodeop, ".arg")) {
            if (phase > phargs)
                gsfatal_bad_input(p, "Too late to add arguments")
            ;
            phase = phargs;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            nregs++;
        } else if (gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")) {
            if (phase > phlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            phase = phlets;

            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            nregs++;

            size += GS_SIZE_BYTECODE(1 + p->numarguments / 2); /* numfields + fields */
        } else if (gssymceq(p->directive, gssymopeprim, gssymcodeop, ".eprim")) {
            struct gsregistered_primset *prims;

            if (phase > phlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            phase = phlets;

            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            nregs++;

            if (prims = gsprims_lookup_prim_set(p->arguments[0]->name)) {
                int nargs;

                /* Ignore free type variables & separator (type erasure) */
                for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
                if (i < p->numarguments) i++;
                nargs = p->numarguments - i;

                size += GS_SIZE_BYTECODE(2 + nargs); /* API index + # args + args */
            } else {
                size += GS_SIZE_BYTECODE(0);
            }
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int nfvs;

            phase = phbytecodes;

            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS);
            nregs++;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nfvs = p->numarguments - i;

            size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
        } else if (gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")) {
            /* no effect on representation */
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            size += GS_SIZE_BYTECODE(1);
            if (p->numarguments > 1)
                gsfatal("%s:%d: Too many arguments to .enter", p->pos.file->name, p->pos.lineno);
            goto done;
        } else if (gssymceq(p->directive, gssymopyield, gssymcodeop, ".yield")) {
            size += GS_SIZE_BYTECODE(1);
            if (p->numarguments > 1)
                gsfatal("%P: Too many arguments to .yield", p->pos);
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int nfvs;

            phase = phbytecodes;

            /* Ignore free type variables & separator (type erasure) */
            for (i = 1; i < p->numarguments && p->arguments[i]->type != gssymseparator; i++);
            if (i < p->numarguments) i++;
            nfvs = p->numarguments - i;

            size += GS_SIZE_BYTECODE(2 + nfvs); /* Code reg + nfvs + fvs */
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".undef")) {
            phase = phbytecodes;
            size += GS_SIZE_BYTECODE(0);
            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsbc_bytecode_size_item (%s)", p->pos, p->directive->name);
        }
#if 0
    next_phase:
        switch (phase) {
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

#endif
    }
done:

    if (size % sizeof(void*))
        size += sizeof(void*) - size % sizeof(void*)
    ;

    return size;
}

/* §section{Actual Byte Compiler} */

static void gsbc_bytecompile_data_item(struct gsfile_symtable *, struct gsparsedline *, struct gsbc_data_locs *, int, int);
static void gsbc_bytecompile_code_item(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco **, int, int);

void
gsbc_bytecompile_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, struct gsbc_data_locs *plocs, struct gsbco **bcos, int n)
{
    int i;
    struct gsparsedfile_segment *pseg;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymtypelable:
            case gssymcoercionlable:
                break;
            case gssymdatalable:
                gsbc_bytecompile_data_item(symtable, items[i].v, plocs, i, n);
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
gsbc_bytecompile_data_item(struct gsfile_symtable *symtable, struct gsparsedline *p, struct gsbc_data_locs *plocs, int i, int n)
{
    if (gssymceq(p->directive, gssymrecord, gssymdatadirective, ".record")) {
        struct gsrecord *record;

        record = (struct gsrecord *)plocs->records[i];
        record->pos = p->pos;
        record->numfields = p->numarguments / 2;
        if (p->numarguments > 0) {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: record fields", p->pos);
        }
    } else if (gssymceq(p->directive, gssymrune, gssymdatadirective, ".rune")) {
        ;
    } else if (gssymceq(p->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        struct gserror *er;

        er = (struct gserror *)plocs->errors[i];
        er->pos = p->pos;
        er->type = gserror_undefined;
    } else if (gssymceq(p->directive, gssymclosure, gssymdatadirective, ".closure")) {
        struct gsheap_item *hp;
        struct gsclosure *cl;

        hp = (struct gsheap_item *)plocs->heap[i];
        memset(&hp->lock, 0, sizeof(hp->lock));
        hp->pos = p->pos;
        hp->type = gsclosure;
        cl = (struct gsclosure *)hp;
        gsargcheck(p, 0, "Code label");
        cl->code = gssymtable_get_code(symtable, p->arguments[0]);
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
    } else if (gssymeq(p->directive, gssymcodedirective, ".eprog")) {
        bcos[i]->tag = gsbc_eprog;
        bcos[i]->pos = p->pos;
        gsbc_byte_compile_api_ops(symtable, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%P: ode directive %s", p->pos, p->directive->name);
    }
}

static
void
gsbc_byte_compile_code_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    enum {
        rttygvars,
        rtgvars,
        rtargs,
        rtlets,
        rtops,
    } phase;
    int i;
    gsvalue *pglobal;
    struct gsbc *pcode;
    int nregs, nglobals, nsubexprs, nargs;
    gsinterned_string regs[MAX_NUM_REGISTERS];

    phase = rttygvars;
    pcode = 0;
    pglobal = (gsvalue*)((uchar*)pbco + sizeof(struct gsbco));
    nregs = nglobals = nsubexprs = nargs = 0;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".tygvar")) {
            if (phase > rttygvars)
                gsfatal_bad_input(p, "Too late to add type global variables")
            ;
            phase = rttygvars;
        } else if (gssymceq(p->directive, gssymopgvar, gssymcodeop, ".gvar")) {
            if (phase > rtgvars)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            phase = rtgvars;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            regs[nregs] = p->label;
            *pglobal++ = gssymtable_get_data(symtable, p->label);
            nregs++;
            nglobals++;
        } else if (gssymeq(p->directive, gssymcodeop, ".arg")) {
            if (phase > rtargs)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            regs[nregs] = p->label;
            nregs++;
            nargs++;
        } else if (gssymceq(p->directive, gssymoprecord, gssymcodeop, ".record")) {
            if (phase > rtlets)
                gsfatal_bad_input(p, "Too late to add allocations");
            phase = rtlets;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            regs[nregs] = p->label;
            if (!pcode)
                pcode = (struct gsbc *)pglobal
            ;
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_record;
            pcode->args[0] = p->numarguments / 2;
            for (i = 0; i < p->numarguments; i += 2) {
                gsfatal_unimpl(__FILE__, __LINE__, "%P: .record fields", p->pos);
            }
            pcode = GS_NEXT_BYTECODE(pcode, 1 + p->numarguments / 2);
            nregs++;
        } else if (gssymceq(p->directive, gssymopeprim, gssymcodeop, ".eprim")) {
            struct gsregistered_primset *prims;

            if (phase > rtlets)
                gsfatal_bad_input(p, "Too late to add allocations")
            ;
            phase = rtlets;

            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            regs[nregs] = p->label;

            if (!pcode)
                pcode = (struct gsbc *)pglobal
            ;
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

                    regarg = gsbc_find_register(p, regs, nregs, p->arguments[i]);
                    pcode->args[2 + i - first_arg] = (uchar)regarg;
                }

                pcode = GS_NEXT_BYTECODE(pcode, 2 + nargs);
            } else {
                pcode->instr = gsbc_op_unknown_eprim;
                pcode = GS_NEXT_BYTECODE(pcode, 0);
            }
            nregs++;
        } else if (gssymceq(p->directive, gssymoplift, gssymcodeop, ".lift")) {
            phase = rtops;
            /* no effect on representation */
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            int reg = 0;

            phase = rtops;
            if (!pcode)
                pcode = (struct gsbc *)pglobal
            ;
            gsargcheck(p, 0, "target");
            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_enter;
            pcode->args[0] = (uchar)reg;
            pcode = GS_NEXT_BYTECODE(pcode, 1);
            goto done;
        } else if (gssymceq(p->directive, gssymopyield, gssymcodeop, ".yield")) {
            int reg;

            phase = rtops;
            if (!pcode)
                pcode = (struct gsbc *)pglobal
            ;
            gsargcheck(p, 0, "target");
            reg = gsbc_find_register(p, regs, nregs, p->arguments[0]);
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_yield;
            pcode->args[0] = (uchar)reg;
            pcode = GS_NEXT_BYTECODE(pcode, 1);
            goto done;
        } else if (gssymeq(p->directive, gssymcodeop, ".undef")) {
            phase = rtops;
            if (!pcode)
                pcode = (struct gsbc *)pglobal
            ;
            pcode->pos = p->pos;
            pcode->instr = gsbc_op_undef;
            pcode = GS_NEXT_BYTECODE(pcode, 0);
            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: Code op %s", p->pos, p->directive->name);
        }
    }

done:

    pbco->numglobals = nglobals;
    pbco->numsubexprs = nsubexprs;
    pbco->numargs = nargs;
}

static
void
gsbc_byte_compile_api_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    enum {
        rttygvars,
        rtgvars,
        rtcode,
        rtargs,
        rtops,
    } phase;
    int i;
    uchar *pout;
    gsvalue *pglobal;
    struct gsbco **psubcode;
    struct gsbc *pcode;
    int nregs, nglobals, ncodes, nargs;
    gsinterned_string regs[MAX_NUM_REGISTERS];
    gsinterned_string codes[MAX_NUM_REGISTERS];

    phase = rttygvars;
    pcode = 0;
    pout = (uchar*)pbco + sizeof(struct gsbco);
    nregs = nglobals = ncodes = nargs = 0;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".subcode")) {
            if (phase > rtcode)
                gsfatal_bad_input(p, "Too late to add sub-expressions")
            ;
            phase = rtcode;
            if (ncodes >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many sub-expressions; max 0x%x", MAX_NUM_REGISTERS)
            ;
            codes[ncodes] = p->label;
            psubcode = (struct gsbco **)pout;
            *psubcode++ = gssymtable_get_code(symtable, p->label);
            pout = (uchar*)psubcode;
            ncodes++;
        } else if (gssymeq(p->directive, gssymcodeop, ".bind")) {
            int creg = 0;
            int nfvs, first_fv;

            phase = rtops;

            pcode = (struct gsbc *)pout;

            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal("%P: Too many registers; max 0x%x", p->pos, MAX_NUM_REGISTERS)
            ;

            regs[nregs] = p->label;

            nregs++;

            creg = gsbc_find_register(p, codes, ncodes, p->arguments[0]);

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
            pout = (uchar *)pcode;
        } else if (gssymeq(p->directive, gssymcodeop, ".body")) {
            int creg = 0;
            int nfvs, first_fv;

            pcode = (struct gsbc *)pout;

            creg = gsbc_find_register(p, codes, ncodes, p->arguments[0]);

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
                gsfatal_unimpl(__FILE__, __LINE__, "%P: store free variables", p->pos);
            }

            pcode = GS_NEXT_BYTECODE(pcode, 2 + nfvs);
            pout = (uchar *)pcode;

            goto done;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%P: API op %s next", p->pos, p->directive->name);
        }
    }

done:

    pbco->numglobals = nglobals;
    pbco->numsubexprs = ncodes;
    pbco->numargs = nargs;
}
