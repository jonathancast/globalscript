#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsheap.h"
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

gsvalue
gsunimplgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(v);
    gsstring_builder_print(err, UNIMPL("GC %s"), block->class->description);
    return 0;
}

gstypecode
gsheapeval(gsvalue val)
{
    gstypecode res;
    struct gsheap_item *hp;

    hp = (struct gsheap_item *)val;
    gsheap_lock(hp);

    res = gsheapstate(hp);

    switch (res) {
        case gstythunk:
            res = ace_start_evaluation(val);
            break;
        case gstystack:
        case gstywhnf:
        case gstyindir:
            break;
        default:
            gswerrstr_unimpl(__FILE__, __LINE__, "gsheapeval(%x; state = %d)", val, res);
            res = gstyenosys;
            break;
    }

    gsheap_unlock(hp);

    return res;
}

gstypecode
gsheapstate(struct gsheap_item *hp)
{
    switch (hp->type) {
        case gsclosure: {
            struct gsclosure *cl;
            struct gsbco *code;

            cl = (struct gsclosure *)hp;
            code = cl->code;

            switch (code->tag) {
                case gsbc_expr: {
                    int fvs_in_cl;
                    int fvs_for_code;
                    int args_for_code;

                    fvs_in_cl = cl->numfvs;
                    fvs_for_code = cl->numfvs;
                    args_for_code = code->numargs;
                    if (fvs_in_cl < fvs_for_code) {
                        gspoison(hp, hp->pos, "Code has %d free variables but closure only has %d", fvs_for_code, fvs_in_cl);
                        return gstyindir;
                    } else if (fvs_in_cl > fvs_for_code + args_for_code) {
                        gspoison(hp, hp->pos, "Code has %d free variables and arguments, but closure has %d arguments supplied", fvs_for_code + args_for_code, fvs_in_cl);
                        return gstyindir;
                    } else if (fvs_in_cl < fvs_for_code + args_for_code) {
                        return gstywhnf;
                    } else {
                        return gstythunk;
                    }
                }
                case gsbc_eprog:
                    return gstywhnf;
                default:
                    gswarning("%s:%d: Evalling something else", __FILE__, __LINE__);
                    gswerrstr_unimpl(__FILE__, __LINE__, "gsheapeval(closure %x; tag = %d)", hp, code->tag);
                    return gstyenosys;
            }
            break;
        }
        case gsapplication:
            return gstythunk;
        case gseval:
            return gstystack;
        case gsindirection:
            return gstyindir;
        default:
            gswerrstr_unimpl(__FILE__, __LINE__, "gsheapeval(%x; type = %d)", hp, hp->type);
            return gstyenosys;
    }
}

void
gsheap_lock(struct gsheap_item *hp)
{
    lock(&hp->lock);
}

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
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Global Script Heap",
};
static void *gsheap_nursury;

void *
gsreserveheap(ulong sz)
{
    return gs_sys_block_suballoc(&gsheap_descr, &gsheap_nursury, sz, sizeof(struct gsbco*));
}

static
gsvalue
gsheapremove_indirections(gsvalue val)
{
    struct gsheap_item *hp;
    struct gsindirection *in;

    hp = (struct gsheap_item *)val;

    lock(&hp->lock);

    if (hp->type != gsindirection) {
        unlock(&hp->lock);
        return val;
    }

    in = (struct gsindirection *)hp;

    val = in->target;
    unlock(&hp->lock);

    return val;
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
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Erroneous Global Script Values",
};
static void *gserrors_nursury;
static Lock gserrors_lock;

void *
gsreserveerrors(ulong sz)
{
    void *res;

    lock(&gserrors_lock);
    res = gs_sys_block_suballoc(&gserrors_descr, &gserrors_nursury, sz, sizeof(gsinterned_string));
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

static gstypecode gsimplerrorseval(gsvalue);
static gsvalue gsimplerrorsindir(gsvalue);

struct gs_block_class gsimplementation_errors_descr = {
    /* evaluator = */ gsimplerrorseval,
    /* indirection_dereferencer = */ gsimplerrorsindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Global Script Implementation Errors",
};
static void *gsimplementation_errors_nursury;
static Lock gsimplementation_errors_lock;

void *
gsreserveimplementation_errors(ulong sz)
{
    void *res;

    lock(&gsimplementation_errors_lock);
    res = gs_sys_block_suballoc(&gsimplementation_errors_descr, &gsimplementation_errors_nursury, sz, sizeof(gsinterned_string));
    unlock(&gsimplementation_errors_lock);
    return res;
}

static
gstypecode gsimplerrorseval(gsvalue val)
{
    return gstyimplerr;
}

static
gsvalue
gsimplerrorsindir(gsvalue val)
{
    return val;
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
        return seprint(buf, ebuf, "Panic: Un-implemented operation in release build: %P: %s", failure->srcpos, failure->message);
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
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Byte-code objects",
};

void *gsbytecode_nursury;

void *
gsreservebytecode(ulong sz)
{
    return gs_sys_block_suballoc(&gsbytecode_desc, &gsbytecode_nursury, sz, sizeof(void*));
}

/* §section Records */

struct gs_block_class gsrecords_descr = {
    /* evaluator = */ gswhnfeval,
    /* indirection_dereferencer = */ gswhnfindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Global Script Records",
};
static void *gsrecords_nursury;
static Lock gsrecords_lock;

void *
gsreserverecords(ulong sz)
{
    void *res;

    lock(&gsrecords_lock);
    res = gs_sys_block_suballoc(&gsrecords_descr, &gsrecords_nursury, sz, sizeof(gsinterned_string));
    unlock(&gsrecords_lock);
    return res;
}

int
gsisrecord_block(struct gs_blockdesc *p)
{
    return p->class == &gsrecords_descr;
}

/* §section Field extraction thunks */

static gstypecode gs_lfield_eval(gsvalue);
static gsvalue gs_lfield_indir(gsvalue);

struct gs_block_class gslfields_descr = {
    /* evaluator = */ gs_lfield_eval,
    /* indirection_dereferencer = */ gs_lfield_indir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Global Script Records",
};
static void *gslfields_nursury;
static Lock gslfields_lock;

enum gslfield_state {
    gslfield_field,
    gslfield_evaluating,
    gslfield_indirection,
};
struct gslfield {
    Lock lock;
    struct gspos pos;
    enum gslfield_state state;
    union {
        struct {
            int fieldno;
            gsvalue record;
        } f;
        struct {
            gsvalue dest;
        } i;
    };
};

gsvalue
gslfield(struct gspos pos, int fieldno, gsvalue record)
{
    struct gslfield *res;

    lock(&gslfields_lock);
    res = gs_sys_block_suballoc(&gslfields_descr, &gslfields_nursury, sizeof(*res), sizeof(gsvalue));
    unlock(&gslfields_lock);

    memset(&res->lock, 0, sizeof(res->lock));
    res->pos = pos;
    res->state = gslfield_field;
    res->f.fieldno = fieldno;
    res->f.record = record;

    return (gsvalue)res;
}

static
gstypecode
gs_lfield_eval(gsvalue v)
{
    struct gslfield *lfield;
    enum gslfield_state st;

    lfield = (struct gslfield *)v;

    lock(&lfield->lock);
    st = lfield->state;
    lfield->state = gslfield_evaluating;
    unlock(&lfield->lock);

    switch (st) {
        case gslfield_field: {
            gstypecode recst;
            gsvalue vrecord;

            vrecord = lfield->f.record;
        again:
            recst = GS_SLOW_EVALUATE(vrecord); /* §tODO Deadlock */
            switch (recst) {
                case gstystack:
                case gstyblocked:
                    lock(&lfield->lock);
                    lfield->state = gslfield_field;
                    lfield->f.record = vrecord;
                    unlock(&lfield->lock);
                    return gstyblocked;
                case gstywhnf: {
                    struct gsrecord *record;

                    record = (struct gsrecord *)vrecord;
                    lock(&lfield->lock);
                    lfield->state = gslfield_indirection;
                    lfield->i.dest = record->fields[lfield->f.fieldno];
                    unlock(&lfield->lock);
                    return gstyindir;
                }
                case gstyindir:
                    vrecord = GS_REMOVE_INDIRECTION(vrecord);
                    goto again;
                case gstyerr:
                case gstyimplerr:
                    lock(&lfield->lock);
                    lfield->state = gslfield_indirection;
                    lfield->i.dest = vrecord;
                    unlock(&lfield->lock);
                    return gstyindir;
                default:
                    lock(&lfield->lock);
                    lfield->state = gslfield_indirection;
                    lfield->i.dest = (gsvalue)gsunimpl(__FILE__, __LINE__, lfield->pos, "gs_lfield_eval: state of record is %d", recst);
                    unlock(&lfield->lock);
                    return gstyindir;
            }
        }
        case gslfield_indirection:
            lock(&lfield->lock);
            lfield->state = gslfield_indirection;
            unlock(&lfield->lock);
            return gstyindir;
        default:
            lock(&lfield->lock);
            lfield->state = gslfield_indirection;
            lfield->i.dest = (gsvalue)gsunimpl(__FILE__, __LINE__, lfield->pos, "gs_lfield_eval: state of lfield is %d", st);
            unlock(&lfield->lock);
            return gstyindir;
    }
}

static
gsvalue
gs_lfield_indir(gsvalue v)
{
    struct gslfield *lfield;

    lfield = (struct gslfield *)v;

    return lfield->i.dest;
}

/* §section Constructors */

static struct gs_sys_global_block_suballoc_info gsconstrs_alloc_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Global Script Constructors",
    },
};

void *
gsreserveconstrs(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsconstrs_alloc_info, sz);
}

int
gsisconstr_block(struct gs_blockdesc *p)
{
    return p->class == &gsconstrs_alloc_info.descr;
}

/* §section API Primitives */

static struct gs_sys_global_block_suballoc_info gseprims_alloc_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "API Primitives",
    },
};

void *
gsreserveeprims(ulong sz)
{
    return gs_sys_global_block_suballoc(&gseprims_alloc_info, sz);
}

int
gsiseprim_block(struct gs_blockdesc *p)
{
    return p->class == &gseprims_alloc_info.descr;
}
