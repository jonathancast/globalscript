#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsheap.h"
#include "gsbytecode.h"
#include "ace.h"

gstypecode
gsnoeval(struct gspos pos, gsvalue val)
{
    return gstyindir;
}

gsvalue
gsnoindir(struct gspos pos, gsvalue val)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(val);
    return (gsvalue)gsunimpl(__FILE__, __LINE__, pos, "gsnoindir: %s", block->class->description);
}

gsvalue
gsunimplgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(v);
    gsstring_builder_print(err, "GC '%s' next", block->class->description);
    return 0;
}

gstypecode
gsheapeval(struct gspos pos, gsvalue val)
{
    gstypecode res;
    struct gsheap_item *hp;

    hp = (struct gsheap_item *)val;
    gsheap_lock(hp);

    res = gsheapstate(pos, hp);

    switch (res) {
        case gstythunk:
            res = ace_start_evaluation(pos, hp);
            break;
        case gstystack:
        case gstywhnf:
        case gstyindir:
            gsheap_unlock(hp);
            break;
        default:
            gsheap_unlock(hp);
            werrstr(UNIMPL("%P: gsheapeval(%x; state = %d)"), pos, val, res);
            res = gstyenosys;
            break;
    }

    return res;
}

gstypecode
gsheapstate(struct gspos pos, struct gsheap_item *hp)
{
    switch (hp->type) {
        case gsclosure: {
            struct gsclosure *cl;
            struct gsbco *code;

            cl = (struct gsclosure *)hp;
            code = cl->cl.code;

            switch (code->tag) {
                case gsbc_expr: {
                    int fvs_in_cl;
                    int fvs_for_code;
                    int args_for_code;

                    fvs_in_cl = cl->cl.numfvs;
                    fvs_for_code = code->numfvs;
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
                case gsbc_impprog:
                    return gstywhnf;
                default:
                    gswarning("%s:%d: %P: Evalling something else", __FILE__, __LINE__, pos);
                    werrstr(UNIMPL("%P: gsheapeval(closure %x; tag = %d)"), pos, hp, code->tag);
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
            werrstr(UNIMPL("%P: gsheapeval(%x; type = %d)"), pos, hp, hp->type);
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
gsevalunboxed(struct gspos pos, gsvalue val)
{
    return gstyunboxed;
}

gstypecode
gswhnfeval(struct gspos pos, gsvalue val)
{
    return gstywhnf;
}

gsvalue
gswhnfindir(struct gspos pos, gsvalue val)
{
    return val;
}

static gsvalue gsheapremove_indirections(struct gspos, gsvalue val);
static gsvalue gsheapgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gsheap_info = {
    /* descr = */ {
        /* evaluator = */ gsheapeval,
        /* indirection_dereferencer = */ gsheapremove_indirections,
        /* gc_trace = */ gsheapgc,
        /* description = */ "Global Script Heap",
    },
};

void *
gsreserveheap(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsheap_info, sz);
}

static
gsvalue
gsheapremove_indirections(struct gspos pos, gsvalue val)
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

    val = in->dest;
    unlock(&hp->lock);

    return val;
}

static
gsvalue
gsheapgc(struct gsstringbuilder *err, gsvalue v)
{
    int i;
    struct gsheap_item *hp;
    gsvalue gctemp;
    ulong sz;

    hp = (struct gsheap_item *)v;

    switch (hp->type) {
        case gsclosure: {
            struct gsclosure *cl, *newcl;

            cl = (struct gsclosure *)hp;

            if (cl->cl.numfvs > 0x100) {
                gswarning("%P: claims to have %ud free variables, which is impossible", cl->hp.pos, cl->cl.numfvs);
                return 0;
            }
            sz = sizeof(*newcl) + cl->cl.numfvs * sizeof(gsvalue);
            newcl = (struct gsclosure *)gsreserveheap(sz);

            memcpy(newcl, cl, sz);

            cl->hp.type = gsgcforward;
            cl->gcfwd = (gsvalue)newcl;

            memset(&newcl->hp.lock, 0, sizeof(newcl->hp.lock));
            if (gs_gc_trace_pos(err, &newcl->hp.pos) < 0) return 0;
            if (gs_gc_trace_bco(err, &newcl->cl.code) < 0) return 0;

            for (i = 0; i < newcl->cl.numfvs; i++)
                if (GS_GC_TRACE(err, &newcl->cl.fvs[i]) < 0) return 0
            ;

            return (gsvalue)newcl;
        }
        case gsapplication: {
            struct gsapplication *app, *newapp;
            gsvalue gctemp;

            app = (struct gsapplication *)hp;

            newapp = (struct gsapplication *)gsreserveheap(sizeof(*newapp) + app->app.numargs * sizeof(gsvalue));

            memcpy(newapp, app, sizeof(*newapp) + app->app.numargs * sizeof(gsvalue));

            app->hp.type = gsgcforward;
            app->gcfwd = (gsvalue)newapp;

            memset(&newapp->hp.lock, 0, sizeof(newapp->hp.lock));
            if (gs_gc_trace_pos(err, &newapp->hp.pos) < 0) return 0;
            if (GS_GC_TRACE(err, &newapp->app.fun) < 0) return 0;

            for (i = 0; i < newapp->app.numargs; i++)
                if (GS_GC_TRACE(err, &newapp->app.arguments[i]) < 0) return 0
            ;

            return (gsvalue)newapp;
        }
        case gseval: {
            struct gseval *ev, *newev;

            ev = (struct gseval *)hp;
            newev = (struct gseval *)gsreserveheap(sizeof(*newev));

            memcpy(newev, ev, sizeof(*newev));

            ev->hp.type = gsgcforward;
            ev->gcfwd = (gsvalue)newev;

            memset(&newev->hp.lock, 0, sizeof(newev->hp.lock));
            if (gs_gc_trace_pos(err, &newev->hp.pos) < 0) return 0;
            if (newev->update && gs_sys_block_in_gc_from_space(newev->update) && ace_eval_gc_trace(err, &newev->update) < 0) return 0;

            return (gsvalue)newev;
        }
        case gsindirection: {
            struct gsindirection *in;
            gsvalue res, gctemp;

            in = (struct gsindirection *)hp;
            res = in->dest;

            if (GS_GC_TRACE(err, &res) < 0) return 0;

            return res;
        }
        case gsgcforward: {
            struct gsgcforward *fwd;

            fwd = (struct gsgcforward *)hp;
            return fwd->gcfwd;
        }
        default:
            gsstring_builder_print(err, UNIMPL("gsheapgc: type = %d"), hp->type);
            return 0;
    }
}

int
gsisheap_block(struct gs_blockdesc *p)
{
    return p->class == &gsheap_info.descr;
}

static gstypecode gserrorseval(struct gspos, gsvalue);
static gsvalue gserrorsindir(struct gspos, gsvalue);
static gsvalue gserrorsgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gserrors_info = {
    /* descr = */ {
        /* evaluator = */ gserrorseval,
        /* indirection_dereferencer = */ gserrorsindir,
        /* gc_trace = */ gserrorsgc,
        /* description = */ "Erroneous Global Script Values",
    },
};

void *
gsreserveerrors(ulong sz)
{
    return gs_sys_global_block_suballoc(&gserrors_info, MAX(sizeof(struct gserror_forward), sz));
}

struct gserror *
gsundefined(struct gspos pos)
{
    struct gserror *res;

    res = gsreserveerrors(sizeof(*res));
    res->pos = pos;
    res->type = gserror_undefined;

    return res;
}

static
gstypecode gserrorseval(struct gspos pos, gsvalue val)
{
    return gstyerr;
}

static
gsvalue
gserrorsindir(struct gspos pos, gsvalue val)
{
    return val;
}

static
gsvalue
gserrorsgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gserror *gserr, *newerr;
    struct gserror_forward *fwd;

    gserr = (struct gserror *)v;

    switch (gserr->type) {
        case gserror_undefined: {
            newerr = gsreserveerrors(MAX(sizeof(*newerr), sizeof(*fwd)));

            memcpy(newerr, gserr, sizeof(*newerr));

            gserr->type = gserror_forward;
            fwd = (struct gserror_forward *)gserr;
            fwd->dest = (gsvalue)newerr;

            if (gs_gc_trace_pos(err, &newerr->pos) < 0) return 0;

            break;
        }
        case gserror_generated: {
            struct gserror_message *msg, *newmsg;

            msg = (struct gserror_message *)gserr;

            newerr = gsreserveerrors(MAX(sizeof(*newmsg) + strlen(msg->message) + 1, sizeof(*fwd)));
            newmsg = (struct gserror_message *)newerr;

            memcpy(newerr, gserr, sizeof(*newmsg) + strlen(msg->message) + 1);

            gserr->type = gserror_forward;
            fwd = (struct gserror_forward *)gserr;
            fwd->dest = (gsvalue)newerr;

            if (gs_gc_trace_pos(err, &newerr->pos) < 0) return 0;

            break;
        }
        case gserror_forward: {
            struct gserror_forward *fwd;

            fwd = (struct gserror_forward *)gserr;

            return fwd->dest;
        }
        default:
            gsstring_builder_print(err, UNIMPL("gserrorsgc: unknown type %d"), gserr->type);
            return 0;
    }

    return (gsvalue)newerr;
}

struct gserror *
gserror(struct gspos pos, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;
    struct gserror *err;
    struct gserror_message *msg;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    err = gsreserveerrors(sizeof(*msg) + strlen(buf) + 1);
    msg = (struct gserror_message *)err;
    err->pos = pos;
    err->type = gserror_generated;
    strcpy(msg->message, buf);

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
    in->dest = (gsvalue)err;
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
        case gserror_generated: {
            struct gserror_message *msg = (struct gserror_message *)p;
            return seprint(buf, ebuf, "%P: %s", p->pos, msg->message);
        }
        default:
            return seprint(buf, ebuf, "%s:%d: gsprint(error type = %d) next", __FILE__, __LINE__, p->type);
    }
}

int
gsiserror_block(struct gs_blockdesc *p)
{
    return p->class == &gserrors_info.descr;
}

/* §section Global Script Implementation Errors */

static gstypecode gsimplerrorseval(struct gspos pos, gsvalue);
static gsvalue gsimplerrorsindir(struct gspos pos, gsvalue);
static gsvalue gsimplerrorsgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gsimplementation_errors_info = {
    /* descr = */ {
        /* evaluator = */ gsimplerrorseval,
        /* indirection_dereferencer = */ gsimplerrorsindir,
        /* gc_trace = */ gsimplerrorsgc,
        /* description = */ "Global Script Implementation Errors",
    },
};

void *
gsreserveimplementation_errors(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsimplementation_errors_info, sz);
}

static
gstypecode gsimplerrorseval(struct gspos pos, gsvalue val)
{
    return gstyimplerr;
}

static
gsvalue
gsimplerrorsindir(struct gspos pos, gsvalue val)
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
    failure->cpos.columnno = 0;
    failure->srcpos = srcpos;
    failure->forward = 0;
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
    return p->class == &gsimplementation_errors_info.descr;
}

gsvalue
gsimplerrorsgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gsimplementation_failure *failure, *newfailure;

    failure = (struct gsimplementation_failure *)v;
    if (failure->forward) {
        gsstring_builder_print(err, UNIMPL("gsimplerrorsgc: Check for forward"));
        return 0;
    }

    newfailure = gsreserveimplementation_errors(sizeof(*newfailure) + strlen(failure->message) + 1);
    memcpy(newfailure, failure, sizeof(*newfailure) + strlen(failure->message) + 1);
    newfailure->forward = 0;

    failure->forward = newfailure;

    if (gs_gc_trace_pos(err, &newfailure->cpos) < 0) return 0;
    if (gs_gc_trace_pos(err, &newfailure->srcpos) < 0) return 0;

    return (gsvalue)newfailure;
}

/* §section Byte Code */

struct gs_sys_global_block_suballoc_info gsbytecode_info = {
    /* desc = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Byte-code objects",
    },
};

void *
gsreservebytecode(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsbytecode_info, sz);
}

struct gsbco_forward {
    struct gsbco bco;
    struct gsbco *dest;
};

static int gs_gc_trace_bco_instrs(struct gsstringbuilder *, void *, void *, void *, void *);

int
gs_gc_trace_bco(struct gsstringbuilder *err, struct gsbco **ppbco)
{
    struct gsbco *bco, *newbco;
    void *enewbco;
    struct gsbco_forward *fwd;
    void *pin;
    gsvalue gctemp;
    int i;

    bco = *ppbco;

    if (CLASS_OF_BLOCK_CONTAINING(bco) != &gsbytecode_info.descr) {
        gsstring_builder_print(err, "gs_gc_trace_bco: tracing bco pointer %p (*%p, in a %s), but doesn't point to bco block, but to a %s instead", bco, ppbco, CLASS_OF_BLOCK_CONTAINING(ppbco)->description, CLASS_OF_BLOCK_CONTAINING(bco)->description);
        return -1;
    }

    if (bco->tag == gsbc_gcforward) {
        fwd = (struct gsbco_forward *)bco;
        *ppbco = fwd->dest;
        return 0;
    }

    newbco = gsreservebytecode(bco->size);
    enewbco = (uchar*)newbco + bco->size;
    memcpy(newbco, bco, bco->size);

    if (bco->size < sizeof(struct gsbco_forward)) {
        gsstring_builder_print(err, UNIMPL("gs_gc_trace_bco: bco isn't large enough to store forwarding pointer"));
        return -1;
    }
    bco->tag = gsbc_gcforward;
    fwd = (struct gsbco_forward *)bco;
    fwd->dest = newbco;

    if (gs_gc_trace_pos(err, &newbco->pos) < 0) return -1;

    pin = (uchar*)newbco + sizeof(struct gsbco);

    if (newbco->numsubexprs > 0x100) {
      gsstring_builder_print(err, "%P: claims to have %ux subcodes, which is impossible", newbco->pos, newbco->numsubexprs);
      return -1;
    }
    for (i = 0; i < newbco->numsubexprs; i++) {
        struct gsbco **psubcode = (struct gsbco **)pin;
        if ((uchar*)psubcode >= (uchar*)enewbco) {
            gsstring_builder_print(err, "%P: ran out of bco after %ux of %ux subcodes, which is impossible", newbco->pos, i, newbco->numsubexprs);
        }
        if (gs_gc_trace_bco(err, psubcode) < 0) return -1;
        pin = psubcode + 1;
    }

    if (newbco->numglobals > 0x100) {
      gsstring_builder_print(err, "%P: claims to have %ux global variables, which is impossible", newbco->pos, newbco->numglobals);
      return -1;
    }
    for (i = 0; i < newbco->numglobals; i++) {
        gsvalue *pglobal = (gsvalue *)pin;
        if ((uchar*)pglobal >= (uchar*)enewbco) {
            gsstring_builder_print(err, "%P: ran out of bco after %ux of %ux global variables, which is impossible", newbco->pos, i, newbco->numglobals);
        }
        if (GS_GC_TRACE(err, pglobal) < 0) return -1;
        pin = pglobal + 1;
    }

    if ((uchar*)pin + sizeof(struct gsbc) > (uchar*)enewbco) {
        gsstring_builder_print(err, "%P: ran out of bco before first instruction, which is impossible", newbco->pos);
        return -1;
    }
    if (gs_gc_trace_bco_instrs(err, bco, newbco, enewbco, pin) < 0) return -1;

    *ppbco = newbco;
    return 0;
}

int
gs_gc_trace_bco_instrs(struct gsstringbuilder *err, void *oldbase, void *newbase, void *enewbase, void *pin)
{
    int i;

    for (;;) {
        struct gsbc *ip = (struct gsbc *)pin;
        if (gs_gc_trace_pos(err, &ip->pos) < 0) return -1;
        switch (ip->instr) {
            case gsbc_op_efv:
                pin = ACE_EFV_SKIP(ip);
                break;
            case gsbc_op_closure:
                pin = ACE_CLOSURE_SKIP(ip);
                break;
            case gsbc_op_unknown_prim:
                pin = ACE_UNKNOWN_PRIM_SKIP(ip);
                break;
            case gsbc_op_prim:
                pin = ACE_PRIM_SKIP(ip);
                break;
            case gsbc_op_constr:
                pin = ACE_CONSTR_SKIP(ip);
                break;
            case gsbc_op_record:
                pin = ACE_RECORD_SKIP(ip);
                break;
            case gsbc_op_field:
                pin = ACE_FIELD_SKIP(ip);
                break;
            case gsbc_op_lfield:
                pin = ACE_LFIELD_SKIP(ip);
                break;
            case gsbc_op_undefined:
                pin = ACE_UNDEFINED_SKIP(ip);
                break;
            case gsbc_op_alias:
                pin = ACE_ALIAS_SKIP(ip);
                break;
            case gsbc_op_apply:
                pin = ACE_APPLY_SKIP(ip);
                break;
            case gsbc_op_unknown_api_prim:
                pin = ACE_UNKNOWN_IMPPRIM_SKIP(ip);
                break;
            case gsbc_op_api_prim:
                pin = ACE_IMPPRIM_SKIP(ip);
                break;
            case gsbc_op_bind_closure:
                pin = ACE_BIND_CLOSURE_SKIP(ip);
                break;
            case gsbc_op_bind_apply:
                pin = ACE_BIND_APPLY_SKIP(ip);
                break;
            case gsbc_op_app:
                pin = ACE_APP_SKIP(ip);
                break;
            case gsbc_op_force:
                pin = ACE_FORCE_SKIP(ip);
                break;
            case gsbc_op_strict:
                pin = ACE_STRICT_SKIP(ip);
                break;
            case gsbc_op_ubanalzye:
                pin = ACE_UBANALYZE_SKIP(ip);
                break;
            case gsbc_op_analyze: {
                struct gsbc **pcases;
                int ncases;

                pcases = ACE_ANALYZE_CASES(ip);
                pcases[0] = (void*)((uchar*)newbase + ((uchar*)pcases[0] - (uchar*)oldbase));
                ncases = (struct gsbc **)pcases[0] - pcases;
                if (ncases >= 0x100) {
                    gsstring_builder_print(err, "%P: appears to have %ux cases, which is impossible", ip->pos, ncases);
                }
                for (i = 1; i < ncases; i++)
                    pcases[i] = (void*)((uchar*)newbase + ((uchar*)pcases[i] - (uchar*)oldbase))
                ;
                for (i = 0; i < ncases; i++) {
                    if ((uchar*)pcases[i] < (uchar*)ip) {
                        gsstring_builder_print(err, "%P: case %d is below the .analyze instruction, which is impossible", ip->pos, i);
                        return -1;
                    }
                    if ((uchar*)pcases[i] + sizeof(struct gsbc) > (uchar*)enewbase) {
                        gsstring_builder_print(err, "%P: ran out of bco before first instruction, of case %d, which is impossible", ip->pos, i);
                        return -1;
                    }
                    if (gs_gc_trace_bco_instrs(err, oldbase, newbase, enewbase, pcases[i]) < 0) return -1;
                }
                return 0;
            }
            case gsbc_op_danalyze: {
                struct gsbc **pcases;
                int ncases;

                pcases = ACE_DANALYZE_CASES(ip);
                ncases = ACE_DANALYZE_NUMCONSTRS(ip);

                for (i = 0; i < ncases + 1; i++) {
                    pcases[i] = (void*)((uchar*)newbase + ((uchar*)pcases[i] - (uchar*)oldbase));
                    if ((uchar*)pcases[i] < (uchar*)ip) {
                        gsstring_builder_print(err, "%P: case %d is below the .analyze instruction, which is impossible", ip->pos, i);
                        return -1;
                    }
                    if ((uchar*)pcases[i] + sizeof(struct gsbc) > (uchar*)enewbase) {
                        gsstring_builder_print(err, "%P: ran out of bco before first instruction, of case %d, which is impossible", ip->pos, i);
                        return -1;
                    }
                    if (gs_gc_trace_bco_instrs(err, oldbase, newbase, enewbase, pcases[i]) < 0) return -1;
                }
            }
            case gsbc_op_undef:
            case gsbc_op_enter:
            case gsbc_op_yield:
            case gsbc_op_ubprim:
            case gsbc_op_unknown_ubprim:
            case gsbc_op_lprim:
            case gsbc_op_unknown_lprim:
            case gsbc_op_body_closure:
            case gsbc_op_body_undefined:
            case gsbc_op_body_alias:
            case gsbc_op_body_apply:
                return 0;
            default:
                gsstring_builder_print(err, UNIMPL("%P: Skip instruction %d"), ip->pos, ip->instr);
                return -1;
        }
        if ((uchar*)pin + sizeof(struct gsbc) > (uchar*)enewbase) {
            gsstring_builder_print(err, "%P: ran out of bco before next instruction, which is impossible", ip->pos);
            return -1;
        }
    }
}

/* §section Records */

static gsvalue gsrecordsgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gsrecords_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsrecordsgc,
        /* description = */ "Global Script Records",
    },
};

void *
gsreserverecords(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsrecords_info, sz);
}

struct gsrecord_gcforward {
    struct gsrecord rec;
    struct gsrecord *dest;
};

static
gsvalue
gsrecordsgc(struct gsstringbuilder *err, gsvalue v)
{
    int i;
    struct gsrecord *rec, *newrec;
    gsvalue gctemp;

    rec = (struct gsrecord *)v;

    switch (rec->type) {
        case gsrecord_fields: {
            struct gsrecord_fields *fields, *newfields;
            ulong sz;
            struct gsrecord_gcforward *fwd;

            fields = (struct gsrecord_fields *)rec;

            sz = sizeof(*newfields) + fields->numfields * sizeof(gsvalue);
            newfields = gsreserverecords(sz);
            newrec = (struct gsrecord *)newfields;

            memcpy(newrec, rec, sz);

            rec->type = gsrecord_gcforward;
            fwd = (struct gsrecord_gcforward *)rec;
            fwd->dest = newrec;

            if (gs_gc_trace_pos(err, &newrec->pos) < 0) return 0;

            for (i = 0; i < newfields->numfields; i++)
                if (GS_GC_TRACE(err, &newfields->fields[i]) < 0) return 0
            ;

            break;
        }
        case gsrecord_gcforward: {
            struct gsrecord_gcforward *fwd;

            fwd = (struct gsrecord_gcforward *)rec;
            return (gsvalue)fwd->dest;
        }
        default:
            gsstring_builder_print(err, UNIMPL("%P: gsrecordsgc: type = %d"), rec->pos, rec->type);
            return 0;
    }

    return (gsvalue)newrec;
}

int
gsisrecord_block(struct gs_blockdesc *p)
{
    return p->class == &gsrecords_info.descr;
}

/* §section Field extraction thunks */

static gstypecode gs_lfield_eval(struct gspos pos, gsvalue);
static gsvalue gs_lfield_indir(struct gspos pos, gsvalue);
static gsvalue gs_lfield_trace(struct gsstringbuilder *, gsvalue);

struct gs_sys_global_block_suballoc_info gslfields_info = {
    /* descr = */ {
        /* evaluator = */ gs_lfield_eval,
        /* indirection_dereferencer = */ gs_lfield_indir,
        /* gc_trace = */ gs_lfield_trace,
        /* description = */ "Global Script Field Extraction Thunks",
    },
};

enum gslfield_state {
    gslfield_field,
    gslfield_evaluating,
    gslfield_indirection,
    gslfield_forward,
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
        struct {
            struct gslfield *dest;
        } fwd;
    };
};

gsvalue
gslfield(struct gspos pos, int fieldno, gsvalue record)
{
    struct gslfield *res;

    res = gs_sys_global_block_suballoc(&gslfields_info, sizeof(*res));

    memset(&res->lock, 0, sizeof(res->lock));
    res->pos = pos;
    res->state = gslfield_field;
    res->f.fieldno = fieldno;
    res->f.record = record;

    return (gsvalue)res;
}

static
gstypecode
gs_lfield_eval(struct gspos pos, gsvalue v)
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
            recst = GS_SLOW_EVALUATE(lfield->pos, vrecord); /* §tODO Deadlock */
            switch (recst) {
                case gstystack:
                case gstyblocked:
                    lock(&lfield->lock);
                    lfield->state = gslfield_field;
                    lfield->f.record = vrecord;
                    unlock(&lfield->lock);
                    return gstyblocked;
                case gstywhnf: {
                    struct gsrecord_fields *record;

                    record = (struct gsrecord_fields *)vrecord;
                    lock(&lfield->lock);
                    lfield->state = gslfield_indirection;
                    lfield->i.dest = record->fields[lfield->f.fieldno];
                    unlock(&lfield->lock);
                    return gstyindir;
                }
                case gstyindir:
                    vrecord = GS_REMOVE_INDIRECTION(lfield->pos, vrecord);
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
gs_lfield_indir(struct gspos pos, gsvalue v)
{
    struct gslfield *lfield;

    lfield = (struct gslfield *)v;

    return lfield->i.dest;
}

static
gsvalue
gs_lfield_trace(struct gsstringbuilder *err, gsvalue v)
{
    struct gslfield *lfield, *newlfield;
    gsvalue gctemp;

    lfield = (struct gslfield *)v;

    switch(lfield->state) {
        case gslfield_field: {
            int fieldno;
            gsvalue recordv;

            fieldno = lfield->f.fieldno;
            recordv = lfield->f.record;

            if (gsisrecord_block(BLOCK_CONTAINING(recordv))) {
                struct gsrecord *rec;
                struct gsrecord_fields *fields;
                gsvalue gsv;

                rec = (struct gsrecord *)recordv;
                switch (rec->type) {
                    case gsrecord_fields:
                        fields = (struct gsrecord_fields *)rec;
                        break;
                    case gsrecord_gcforward: {
                        struct gsrecord_gcforward *fwd;

                        fwd = (struct gsrecord_gcforward *)rec;
                        if (fwd->dest->type != gsrecord_fields) {
                            gsstring_builder_print(err, UNIMPL("gs_lfield_trace: redex; GC forward doesn't point at record value"));
                            return 0;
                        }
                        fields = (struct gsrecord_fields *)fwd->dest;
                        break;
                    }
                    default:
                        gsstring_builder_print(err, UNIMPL("gs_lfield_trace: redex; record type %d"), rec->type);
                        return 0;
                }
                gsv = fields->fields[fieldno];
                if (GS_GC_TRACE(err, &gsv) < 0) return 0;
                return gsv;
            }

            newlfield = gs_sys_global_block_suballoc(&gslfields_info, sizeof(*newlfield));

            memcpy(newlfield, lfield, sizeof(*newlfield));
            memset(&newlfield->lock, 0, sizeof(newlfield->lock));

            lfield->state = gslfield_forward;
            lfield->fwd.dest = newlfield;

            if (gs_gc_trace_pos(err, &newlfield->pos) < 0) return 0;
            if (GS_GC_TRACE(err, &newlfield->f.record) < 0) return 0;

            return (gsvalue)newlfield;
        }
        case gslfield_indirection: {
            gsvalue dest;

            dest = lfield->i.dest;

            if (GS_GC_TRACE(err, &dest) < 0) return 0;

            return dest;
        }
        case gslfield_forward: {
            return (gsvalue)lfield->fwd.dest;
        }
        default:
            gsstring_builder_print(err, UNIMPL("gs_lfield_trace: state = %d"), lfield->state);
            return 0;
    }
}

/* §section Constructors */

static gsvalue gsconstrsgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gsconstrs_alloc_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsconstrsgc,
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

static
gsvalue
gsconstrsgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gsconstr *constr, *newconstr;
    gsvalue gctemp;
    int i;

    constr = (struct gsconstr *)v;
    switch (constr->type) {
        case gsconstr_args: {
            newconstr = gsreserveconstrs(sizeof(*newconstr) + constr->a.numargs * sizeof(gsvalue));

            memcpy(newconstr, constr, sizeof(*newconstr) + constr->a.numargs * sizeof(gsvalue));

            constr->type = gsconstr_gcforward;
            constr->f.dest = newconstr;

            if (gs_gc_trace_pos(err, &newconstr->pos) < 0) return 0;

            for (i = 0; i < newconstr->a.numargs; i++)
                if (GS_GC_TRACE(err, &newconstr->a.arguments[i]) < 0) return 0
            ;

            break;
        }
        case gsconstr_gcforward: {
            return (gsvalue)constr->f.dest;
        }
        default:
            gsstring_builder_print(err, UNIMPL("%P: gsconstrsgc: type = %d"), constr->pos, constr->type);
            return 0;
    }

    return (gsvalue)newconstr;
}

/* §section API Primitives */

static gsvalue gsapiprimgc(struct gsstringbuilder *, gsvalue);

static struct gs_sys_global_block_suballoc_info gsapiprims_alloc_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsapiprimgc,
        /* description = */ "API Primitives",
    },
};

void *
gsreserveapiprims(ulong sz)
{
    return gs_sys_global_block_suballoc(&gsapiprims_alloc_info, sz);
}

int
gsisapiprim_block(struct gs_blockdesc *p)
{
    return p->class == &gsapiprims_alloc_info.descr;
}

static
gsvalue
gsapiprimgc(struct gsstringbuilder *err, gsvalue v)
{
    struct gsapiprim *ep, *newep;
    ulong sz;
    gsvalue gctemp;
    int i;

    ep = (struct gsapiprim *)v;

    if (ep->type == apiprim_forward) return (gsvalue)ep->f.dest;

    sz = sizeof(*newep) + ep->p.numargs * sizeof(gsvalue);
    newep = gsreserveapiprims(sz);

    memcpy(newep, ep, sz);

    ep->type = apiprim_forward;
    ep->f.dest = newep;

    if (gs_gc_trace_pos(err, &newep->pos) < 0) return 0;
    for (i = 0; i < newep->p.numargs; i++)
        if (GS_GC_TRACE(err, &newep->p.arguments[i]) < 0)
            return 0
    ;

    return (gsvalue)newep;
}
