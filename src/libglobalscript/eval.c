#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsheap.h"
#include "gsregtables.h"
#include "ace.h"

gstypecode
gs_get_gsvalue_state(gsvalue val)
{
    gsfatal("gs_get_gsvalue_state(%x) next", val);

    return 0;
}

gstypecode
gsnoeval(gsvalue val)
{
    gsfatal("Tried to evaluate a gsvalue which doesn't point into an evaluable block!");
    return 0;
}

static int gsheap_lock(struct gsheap_item *);
static void gsheap_unlock(struct gsheap_item *);

gstypecode
gsheapeval(gsvalue val)
{
    gstypecode res;
    struct gsheap_item *hp;
    int type;

    hp = (struct gsheap_item *)val;
    type = gsheap_lock(hp);

    res = gstyenosys;

    switch (type) {
        case gsclosure: {
            struct gsclosure *cl;
            struct gsbco *code;

            cl = (struct gsclosure *)hp;
            code = cl->code;

            switch (code->tag) {
                case gsbc_expr:
                    {
                        int fvs_in_cl;
                        int fvs_for_code;
                        int args_for_code;

                        gswarning("%s:%d: Getting number of free variables in closure: deferred", __FILE__, __LINE__);
                        fvs_in_cl = 0;
                        gswarning("%s:%d: Getting number of free variables in bco: deferred", __FILE__, __LINE__);
                        fvs_for_code = 0;
                        args_for_code = code->numargs;
                    if (fvs_in_cl < fvs_for_code) {
                        gspoison(hp, code->pos.file, code->pos.lineno, "Code has %d free variables but closure only has %d", fvs_for_code, fvs_in_cl);
                        res = gstywhnf;
                    } else if (fvs_in_cl < fvs_for_code + args_for_code) {
                        res = gstywhnf;
                    } else
                        res = ace_start_evaluation(val)
                    ;
                    }
                    break;
                case gsbc_eprog:
                    res = gstywhnf;
                    break;
                default:
                    gswarning("%s:%d: Evalling something else", __FILE__, __LINE__);
                    gswerrstr_unimpl(__FILE__, __LINE__, "gsheapeval(closure %x; tag = %d)", val, code->tag);
                    res = gstyenosys;
                    break;
            }
        }
        break;
        case gseval:
            res = gstystack;
            break;
        case gsindirection:
            res = gstywhnf;
            break;
        default:
            gswerrstr_unimpl(__FILE__, __LINE__, "gsheapeval(%x; type = %d)", val, type);
            res = gstyenosys;
            break;
    }

    gsheap_unlock(hp);

    return res;
}

static
int
gsheap_lock(struct gsheap_item *hp)
{
    lock(&hp->lock);
    return hp->type;
}

static
void
gsheap_unlock(struct gsheap_item *hp)
{
    unlock(&hp->lock);
}

gstypecode
gsevalunboxed(gsvalue val)
{
    return gstyunboxed;
}

gstypecode
gswhnfeval(gsvalue val)
{
    return gstywhnf;
}

static struct gs_block_class gsheap_descr = {
    /* evaluator = */ gsheapeval,
    /* description = */ "Global Script Heap",
};
static void *gsheap_nursury;

void *
gsreserveheap(ulong sz)
{
    return gs_sys_seg_suballoc(&gsheap_descr, &gsheap_nursury, sz, sizeof(struct gsbco*));
}

gsvalue
gsremove_indirections(gsvalue val)
{
    struct gs_blockdesc *block;
    struct gsheap_item *hp;
    struct gsindirection *in;

    for (;;) {
        block = BLOCK_CONTAINING(val);

        if (block->class != &gsheap_descr)
            return val
        ;

        hp = (struct gsheap_item *)val;

        lock(&hp->lock);

        if (hp->type != gsindirection) {
            unlock(&hp->lock);
            return val;
        }

        in = (struct gsindirection *)hp;

        val = in->target;
        unlock(&hp->lock);
    }
}

int
gsisheap_block(struct gs_blockdesc *p)
{
    return p->class == &gsheap_descr;
}

struct gs_block_class gserrors_descr = {
    /* evaluator = */ gswhnfeval,
    /* description = */ "Erroneous Global Script Values",
};
static void *gserrors_nursury;
static Lock gserrors_lock;

void *
gsreserveerrors(ulong sz)
{
    void *res;

    lock(&gserrors_lock);
    res = gs_sys_seg_suballoc(&gserrors_descr, &gserrors_nursury, sz, sizeof(gsinterned_string));
    unlock(&gserrors_lock);
    return res;
}

struct gserror *
gserror(gsinterned_string file, int lineno, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;
    struct gserror *err;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    err = gsreserveerrors(sizeof(*err) + strlen(buf) + 1);
    err->pos.file = file;
    err->pos.lineno = lineno;
    err->type = gserror_generated;
    strcpy(err->message, buf);

    return err;
}

struct gserror *
gserror_unimpl(char *file, int lineno, gsinterned_string srcfile, int srclineno, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    if (gsdebug)
        return gserror(srcfile, srclineno, "%s:%d: %s next", file, lineno, buf)
    ; else
        return gserror(srcfile, srclineno, "Panic: Un-implemented operation in release build: %s", buf)
    ;
}

void
gspoison(struct gsheap_item *hp, gsinterned_string srcfile, int srclineno, char *fmt, ...)
{
    struct gserror *err;
    struct gsindirection *in;

    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    err = gserror(srcfile, srclineno, "%s", buf);

    hp->type = gsindirection;
    in = (struct gsindirection *)hp;
    in->target = (gsvalue)err;
}

void
gspoison_unimpl(struct gsheap_item *hp, char *file, int lineno, struct gspos srcpos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    if (gsdebug)
        gspoison(hp, srcpos.file, srcpos.lineno, "%s:%d: %s next", file, lineno, buf)
    ; else
        gspoison(hp, srcpos.file, srcpos.lineno, "Panic: Un-implemented operation in release build: %s", buf)
    ;
}

int
gsiserror_block(struct gs_blockdesc *p)
{
    return p->class == &gserrors_descr;
}

struct gs_block_class gsbytecode_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Byte-code objects",
};

void *gsbytecode_nursury;

void *
gsreservebytecode(ulong sz)
{
    return gs_sys_seg_suballoc(&gsbytecode_desc, &gsbytecode_nursury, sz, sizeof(void*));
}

struct gs_block_class gsrecords_descr = {
    /* evaluator = */ gswhnfeval,
    /* description = */ "Global Script Records",
};
static void *gsrecords_nursury;
static Lock gsrecords_lock;

void *
gsreserverecords(ulong sz)
{
    void *res;

    lock(&gsrecords_lock);
    res = gs_sys_seg_suballoc(&gsrecords_descr, &gsrecords_nursury, sz, sizeof(gsinterned_string));
    unlock(&gsrecords_lock);
    return res;
}

int
gsisrecord_block(struct gs_blockdesc *p)
{
    return p->class == &gsrecords_descr;
}
