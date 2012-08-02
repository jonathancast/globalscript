/* §source.file{Byte-compiler} */

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
