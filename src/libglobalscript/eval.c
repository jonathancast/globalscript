#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsheap.h"
#include "gsregtables.h"
#include "ace.h"

gstypecode
gsnoeval(gsvalue val)
{
    return gstyindir;
}

gsvalue
gsnoindir(gsvalue val)
{
    struct gs_blockdesc *block;
    struct gspos pos;

    block = BLOCK_CONTAINING(val);
    pos.file = gsintern_string(gssymfilename, __FILE__);
    pos.lineno = __LINE__; return (gsvalue)gsunimpl(__FILE__, __LINE__, pos, "gsnoindir: %s", block->class->description);
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

                        fvs_in_cl = cl->numfvs;
                        fvs_for_code = cl->numfvs;
                        args_for_code = code->numargs;
                        if (fvs_in_cl < fvs_for_code) {
                            gspoison(hp, hp->pos, "Code has %d free variables but closure only has %d", fvs_for_code, fvs_in_cl);
                            res = gstyindir;
                        } else if (fvs_in_cl > fvs_for_code + args_for_code) {
                            gspoison(hp, hp->pos, "Code has %d free variables and arguments, but closure has %d arguments supplied", fvs_for_code + args_for_code, fvs_in_cl);
                            return gstyindir;
                        } else if (fvs_in_cl < fvs_for_code + args_for_code) {
                            res = gstywhnf;
                        } else {
                            res = ace_start_evaluation(val);
                        }
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
            break;
        }
        case gsapplication:
            res = ace_start_evaluation(val);
            break;
        case gseval:
            res = gstystack;
            break;
        case gsindirection:
            res = gstyindir;
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

gsvalue
gswhnfindir(gsvalue val)
{
    return val;
}

static gsvalue gsheapremove_indirections(gsvalue val);

static struct gs_block_class gsheap_descr = {
    /* evaluator = */ gsheapeval,
    /* indirection_dereferencer = */ gsheapremove_indirections,
    /* description = */ "Global Script Heap",
};
static void *gsheap_nursury;

void *
gsreserveheap(ulong sz)
{
    return gs_sys_seg_suballoc(&gsheap_descr, &gsheap_nursury, sz, sizeof(struct gsbco*));
}

static
gsvalue
gsheapremove_indirections(gsvalue val)
{
    struct gs_blockdesc *block;
    struct gsheap_item *hp;
    struct gsindirection *in;

    for (;;) {
        if (!IS_PTR(val))
            return val
        ;

        block = BLOCK_CONTAINING(val);

        if (block->class != &gsheap_descr)
            return GS_REMOVE_INDIRECTIONS(val)
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

static gstypecode gserrorseval(gsvalue);
static gsvalue gserrorsindir(gsvalue);

struct gs_block_class gserrors_descr = {
    /* evaluator = */ gserrorseval,
    /* indirection_dereferencer = */ gserrorsindir,
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

static
gstypecode gserrorseval(gsvalue val)
{
    return gstyerr;
}

static
gsvalue
gserrorsindir(gsvalue val)
{
    return val;
}

struct gserror *
gserror(struct gspos pos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;
    struct gserror *err;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    err = gsreserveerrors(sizeof(*err) + strlen(buf) + 1);
    err->pos = pos;
    err->type = gserror_generated;
    strcpy(err->message, buf);

    return err;
}

void
gspoison(struct gsheap_item *hp, struct gspos srcpos, char *fmt, ...)
{
    struct gserror *err;
    struct gsindirection *in;

    char buf[0x100];
    va_list arg;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    err = gserror(srcpos, "%s", buf);

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
        gspoison(hp, srcpos, "%s:%d: %s next", file, lineno, buf)
    ; else
        gspoison(hp, srcpos, "Panic: Un-implemented operation in release build: %s", buf)
    ;
}

char *
gserror_format(char *buf, char *ebuf, struct gserror *p)
{
    switch (p->type) {
        case gserror_undefined:
            return seprint(buf, ebuf, "%s %P", "undefined", p->pos);
        case gserror_generated:
            return seprint(buf, ebuf, "%P: %s", p->pos, p->message);
        default:
            return seprint(buf, ebuf, "%s:%d: gsprint(error type = %d) next", __FILE__, __LINE__, p->type);
    }
}

int
gsiserror_block(struct gs_blockdesc *p)
{
    return p->class == &gserrors_descr;
}

/* §section Global Script Implementation Errors */

struct gs_block_class gsimplementation_errors_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "Global Script Implementation Errors",
};
static void *gsimplementation_errors_nursury;
static Lock gsimplementation_errors_lock;

void *
gsreserveimplementation_errors(ulong sz)
{
    void *res;

    lock(&gsimplementation_errors_lock);
    res = gs_sys_seg_suballoc(&gsimplementation_errors_descr, &gsimplementation_errors_nursury, sz, sizeof(gsinterned_string));
    unlock(&gsimplementation_errors_lock);
    return res;
}

struct gsimplementation_failure *
gsunimpl(char *file, int lineno, struct gspos srcpos, char *err, ...)
{
    struct gsimplementation_failure *failure;
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    failure = gsreserveimplementation_errors(sizeof(*failure) + strlen(buf) + 1);
    failure->cpos.file = gsintern_string(gssymfilename, file);
    failure->cpos.lineno = lineno;
    failure->srcpos = srcpos;
    strcpy(failure->message, buf);

    return failure;
}

char *
gsimplementation_failure_format(char *buf, char *ebuf, struct gsimplementation_failure *failure)
{
    if (gsdebug) {
        return seprint(buf, ebuf, "%P: %P: %s next", failure->cpos, failure->srcpos, failure->message);
    } else {
        return seprint(buf, ebuf, "Panic: Un-implmented operation in release build: %P: %s", failure->srcpos, failure->message);
    }
}

int
gsisimplementation_failure_block(struct gs_blockdesc *p)
{
    return p->class == &gsimplementation_errors_descr;
}

/* §section Byte Code */

struct gs_block_class gsbytecode_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "Byte-code objects",
};

void *gsbytecode_nursury;

void *
gsreservebytecode(ulong sz)
{
    return gs_sys_seg_suballoc(&gsbytecode_desc, &gsbytecode_nursury, sz, sizeof(void*));
}

/* §section Records */

struct gs_block_class gsrecords_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
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

/* §section Constructors */

struct gs_block_class gsconstrs_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "Global Script Constructors",
};
static void *gsconstrs_nursury;
static Lock gsconstrs_lock;

void *
gsreserveconstrs(ulong sz)
{
    void *res;

    lock(&gsconstrs_lock);
    res = gs_sys_seg_suballoc(&gsconstrs_descr, &gsconstrs_nursury, sz, sizeof(gsinterned_string));
    unlock(&gsconstrs_lock);
    return res;
}

int
gsisconstr_block(struct gs_blockdesc *p)
{
    return p->class == &gsconstrs_descr;
}

/* §section API Primitives */

struct gs_block_class gseprims_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* description = */ "API Primitives",
};
static void *gseprims_nursury;
static Lock gseprims_lock;

void *
gsreserveeprims(ulong sz)
{
    void *res;

    lock(&gseprims_lock);
    res = gs_sys_seg_suballoc(&gseprims_descr, &gseprims_nursury, sz, sizeof(gsinterned_string));
    unlock(&gseprims_lock);
    return res;
}

int
gsiseprim_block(struct gs_blockdesc *p)
{
    return p->class == &gseprims_descr;
}
