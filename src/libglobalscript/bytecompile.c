/* §source.file{Byte-compiler} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsbytecompile.h"
#include "gsheap.h"
#include "gstopsort.h"

static uint gsbc_heap_size_item(struct gsbc_item);
static uint gsbc_error_size_item(struct gsbc_item);
static gsvalue gsbc_get_indir_item(struct gsfile_symtable *, struct gsbc_item);

void
gsbc_alloc_data_for_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, gsvalue *errors, gsvalue *indir, int n)
{
    uint total_heap_size, heap_offsets[MAX_ITEMS_PER_SCC], heap_size[MAX_ITEMS_PER_SCC],
        total_error_size, error_offsets[MAX_ITEMS_PER_SCC], error_size[MAX_ITEMS_PER_SCC]
    ;
    int i;
    void *heap_base, *error_base;

    if (n > MAX_ITEMS_PER_SCC)
        gsfatal("%s:%d: Too many items in SCC; 0x%x items; max 0x%x", __FILE__, __LINE__, n, MAX_ITEMS_PER_SCC);

    total_heap_size = total_error_size = 0;
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

        indir[i] = gsbc_get_indir_item(symtable, items[i]);
    }

    if (total_heap_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total heap size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_heap_size, BLOCK_SIZE)
    ;
    if (total_error_size > BLOCK_SIZE)
        gsfatal("%s:%d: Total error size too large; 0x%x > 0x%x", __FILE__, __LINE__, total_error_size, BLOCK_SIZE)
    ;

    heap_base = gsreserveheap(total_heap_size);
    error_base = gsreserveerrors(total_error_size);

    for (i = 0; i < n; i++) {
        heap[i] = errors[i] = 0;
        if (heap_size[i]) {
            heap[i] = (gsvalue)((uchar*)heap_base + heap_offsets[i]);
            gssymtable_set_data(symtable, items[i].v->label, heap[i]);
        } else if (error_size[i]) {
            errors[i] = (gsvalue)((uchar*)error_base + error_offsets[i]);
            gssymtable_set_data(symtable, items[i].v->label, errors[i]);
        } else if (indir[i]) {
            gssymtable_set_data(symtable, items[i].v->label, indir[i]);
        }
    }
}

static
uint
gsbc_heap_size_item(struct gsbc_item item)
{
    struct gsparsedline *p;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymeq(p->directive, gssymdatadirective, ".undefined")) {
        return 0;
    } else if (gssymeq(p->directive, gssymdatadirective, ".closure")) {
        return sizeof(struct gsclosure) + sizeof(gsvalue);
    } else if (gssymeq(p->directive, gssymdatadirective, ".cast")) {
        return 0;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "gsbc_heap_size_item(%s)", p->directive->name);
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
    if (gssymeq(p->directive, gssymdatadirective, ".undefined")) {
        return sizeof(struct gserror);
    } else if (gssymeq(p->directive, gssymdatadirective, ".closure")) {
        return 0;
    } else if (gssymeq(p->directive, gssymdatadirective, ".cast")) {
        return 0;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, item.v, "gsbc_error_size_item(%s)", p->directive->name);
    }
    return 0;
}

static
gsvalue
gsbc_get_indir_item(struct gsfile_symtable *symtable, struct gsbc_item item)
{
    struct gsparsedline *p;
    gsvalue res;

    if (item.type != gssymdatalable) return 0;

    p = item.v;
    if (gssymeq(p->directive, gssymdatadirective, ".undefined")) {
        return 0;
    } else if (gssymeq(p->directive, gssymdatadirective, ".closure")) {
        return 0;
    } else if (gssymeq(p->directive, gssymdatadirective, ".cast")) {
        res = gssymtable_get_data(symtable, p->arguments[1]);
        if (!res)
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Can't find cast referent %s", p->arguments[1]->name)
        ;
        return res;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_error_size_item(%s)", p->directive->name);
    }
    return 0;
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

#define MAX_NUM_REGISTERS 0x100

static
int
gsbc_bytecode_size_item(struct gsbc_item item)
{
    int size;
    struct gsparsedline *p;
    struct gsparsedfile_segment *pseg;
    enum {
        phgvars,
        phbytecodes,
    } phase;
    int nregs;

    size = sizeof(struct gsbco);

    phase = phgvars;
    nregs = 0;
    pseg = item.pseg;
    for (p = gsinput_next_line(&pseg, item.v); ; p = gsinput_next_line(&pseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
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
                    p->file->name,
                    p->lineno
                )
            ;
            size += sizeof(gsvalue);
            nregs++;
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            size += 1; /* Byte code */
            size += 1; /* Argument */
            if (p->numarguments > 1)
                gsfatal("%s:%d: Too many arguments to .enter", p->file->name, p->lineno);
            goto done;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "gsbc_bytecode_size_item (%s)", p->directive->name);
        }
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

#endif
    }
done:

    if (size % sizeof(void*))
        size += sizeof(void*) - size
    ;

    return size;
}

/* §section{Actual Byte Compiler} */

static void gsbc_bytecompile_data_item(struct gsfile_symtable *, struct gsparsedline *, gsvalue *, gsvalue *, int, int);
static void gsbc_bytecompile_code_item(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco **, int, int);

void
gsbc_bytecompile_scc(struct gsfile_symtable *symtable, struct gsbc_item *items, gsvalue *heap, gsvalue *errors, struct gsbco **bcos, int n)
{
    int i;
    struct gsparsedfile_segment *pseg;

    for (i = 0; i < n; i++) {
        switch (items[i].type) {
            case gssymtypelable:
            case gssymcoercionlable:
                break;
            case gssymdatalable:
                gsbc_bytecompile_data_item(symtable, items[i].v, heap, errors, i, n);
                break;
            case gssymcodelable:
                pseg = items[i].pseg;
                gsbc_bytecompile_code_item(symtable, &pseg, items[i].v, bcos, i, n);
                break;
            default:
                gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v, "gsbc_bytecompile_scc(type = %d)", items[i].type);
        }
    }
}

static
void
gsbc_bytecompile_data_item(struct gsfile_symtable *symtable, struct gsparsedline *p, gsvalue *heap, gsvalue *errors, int i, int n)
{
    if (gssymeq(p->directive, gssymdatadirective, ".undefined")) {
        struct gserror *er;

        er = (struct gserror *)errors[i];
        er->file = p->file;
        er->lineno = p->lineno;
        er->type = gserror_undefined;
    } else if (gssymeq(p->directive, gssymdatadirective, ".closure")) {
        struct gsheap_item *hp;
        struct gsclosure *cl;

        hp = (struct gsheap_item *)heap[i];
        hp->file = p->file;
        hp->lineno = p->lineno;
        hp->type = gsclosure;
        cl = (struct gsclosure *)heap[i];
        gsargcheck(p, 0, "Code label");
        cl->code = gssymtable_get_code(symtable, p->arguments[0]);
    } else if (gssymeq(p->directive, gssymdatadirective, ".cast")) {
        ;
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "Data directive %s next", p->directive->name);
    }
}

static void gsbc_byte_compile_code_ops(struct gsfile_symtable *, struct gsparsedfile_segment **, struct gsparsedline *, struct gsbco *);

static
void
gsbc_bytecompile_code_item(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco **bcos, int i, int n)
{
    if (gssymeq(p->directive, gssymcodedirective, ".expr")) {
        bcos[i]->tag = gsbc_expr;
        gsbc_byte_compile_code_ops(symtable, ppseg, gsinput_next_line(ppseg, p), bcos[i]);
    } else {
        gsfatal_unimpl_input(__FILE__, __LINE__, p, "Code directive %s next", p->directive->name);
    }
}

static
void
gsbc_byte_compile_code_ops(struct gsfile_symtable *symtable, struct gsparsedfile_segment **ppseg, struct gsparsedline *p, struct gsbco *pbco)
{
    enum {
        rtgvars,
        rtops,
    } phase;
    int i;
    gsvalue *pglobal;
    uchar *pcode;
    int nregs, nglobals;
    gsinterned_string regs[MAX_NUM_REGISTERS];

    phase = rtgvars;
    pcode = 0;
    pglobal = (gsvalue*)((uchar*)pbco + sizeof(struct gsbco));
    nregs = nglobals = 0;
    for (; ; p = gsinput_next_line(ppseg, p)) {
        if (gssymeq(p->directive, gssymcodeop, ".gvar")) {
            if (phase > rtgvars)
                gsfatal_bad_input(p, "Too late to add global variables")
            ;
            if (nregs >= MAX_NUM_REGISTERS)
                gsfatal_bad_input(p, "Too many registers; max 0x%x", MAX_NUM_REGISTERS)
            ;
            regs[nregs] = p->label;
            *pglobal++ = gssymtable_get_data(symtable, p->label);
            nregs++;
            nglobals++;
        } else if (gssymeq(p->directive, gssymcodeop, ".enter")) {
            int reg = 0;

            phase = rtops;
            if (!pcode)
                pcode = (uchar*)pglobal
            ;
            gsargcheck(p, 0, "target");
            for (i = 0; i < nregs; i++) {
                if (regs[i] == p->arguments[0]) {
                    reg = i;
                    goto have_register;
                }
            }
            gsfatal_bad_input(p, "Unknown register %s", p->arguments[0]->name);
        have_register: ;
            *pcode++ = gsbc_op_enter;
            *pcode++ = (uchar)reg;
            goto done;
        } else {
            gsfatal_unimpl_input(__FILE__, __LINE__, p, "Code op %s next", p->directive->name);
        }
    }

done:

    pbco->numglobals = nglobals;
}
