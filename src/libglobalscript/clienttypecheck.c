/* §source.file Client-level Type Checker */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

int
gstype_expect_abstract(struct gstype *ty, char *name, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_abstract) {
        struct gstype_abstract *abstract;

        abstract = (struct gstype_abstract *)ty;

        if (strcmp(abstract->name->name, name)) {
            seprint(err, eerr, "I don't think %s is %s", abstract->name->name, name);
            return -1;
        } else {
            return 0;
        }
    } else {
        seprint(err, eerr, "I don't think %s is an abstract type", ty_buf);
        return -1;
    }
}

int
gstype_expect_prim(struct gstype *ty, enum gsprim_type_group group, char *primset, char *primname, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_unprim) {
        struct gstype_unprim *prim;

        prim = (struct gstype_unprim *)ty;

        if (prim->primtypegroup != group) {
            seprint(err, eerr, "I don't think %s is a %s primitive type", ty_buf, "unknown primitive");
            return -1;
        }
        if (strcmp(prim->primsetname->name, primset)) {
            seprint(err, eerr, "I don't think primset %s is the same as primset %s", prim->primsetname->name, primset);
            return -1;
        }
        if (strcmp(prim->primname->name, primname)) {
            seprint(err, eerr, "I don't think prim %s in primset %s is the same as prim %s", prim->primname->name, primset, primname);
            return -1;
        }
        return 0;
    } else if (ty->node == gstype_knprim) {
        struct gstype_knprim *prim;

        prim = (struct gstype_knprim *)ty;

        if (prim->primtypegroup != group) {
            seprint(err, eerr, "I don't think %s is a %s primitive type", ty_buf, "unknown primitive");
            return -1;
        }
        if (strcmp(prim->primset->name, primset)) {
            seprint(err, eerr, "I don't think primset %s is the same as primset %s", prim->primset->name, primset);
            return -1;
        }
        if (strcmp(prim->primname->name, primname)) {
            seprint(err, eerr, "I don't think prim %s in primset %s is the same as prim %s", prim->primname->name, primset, primname);
            return -1;
        }
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a primitive type", ty_buf);
        return -1;
    }
}

int
gstype_expect_var(struct gstype *ty, gsinterned_string v, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_var) {
        struct gstype_var *var;

        var = (struct gstype_var *)ty;

        if (var->name != v) {
            seprint(err, eerr, "I don't think %s is %s", var->name->name, v->name);
            return -1;
        }
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a type variable", ty_buf);
        return -1;
    }
}

int
gstype_expect_forall(struct gstype *ty, gsinterned_string *pv, struct gstype **pbody, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_forall) {
        struct gstype_forall *forall;

        forall = (struct gstype_forall *)ty;

        *pv = forall->var;
        *pbody = forall->body;
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a ∀ type", ty_buf);
        return -1;
    }
}

int
gstype_expect_lift(struct gstype *ty, struct gstype **tyarg, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_lift) {
        struct gstype_lift *lift;

        lift = (struct gstype_lift *)ty;

        *tyarg = lift->arg;
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a lifted type", ty_buf);
        return -1;
    }
}

int
gstype_expect_app(struct gstype *ty, struct gstype **tyfun, struct gstype **tyarg, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_app) {
        struct gstype_app *app;

        app = (struct gstype_app *)ty;

        *tyfun = app->fun;
        *tyarg = app->arg;
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is an application", ty_buf);
        return -1;
    }
}

int
gstype_expect_fun(struct gstype *ty, struct gstype **ptyarg, struct gstype **ptyres, char *err, char *eerr)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_fun) {
        struct gstype_fun *fun;

        fun = (struct gstype_fun *)ty;

        *ptyarg = fun->tyarg;
        *ptyres = fun->tyres;
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a function type", ty_buf);
        return -1;
    }
}

int
gstype_expect_product(struct gstype *ty, char *err, char *eerr, int nfields, ...)
{
    va_list arg;
    char ty_buf[0x100];
    int i;

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %P", __FILE__, __LINE__, ty->pos);
        return -1;
    }

    if (ty->node == gstype_product) {
        va_start(arg, nfields);
        for (i = 0; i < nfields; i++) {
            seprint(err, eerr, "%s:%d: %P: Check fields in gstype_expect_product next", __FILE__, __LINE__, ty->pos);
            va_end(arg);
            return -1;
        }
        va_end(arg);
        return 0;
    } else {
        seprint(err, eerr, "I don't think %s is a product type", ty_buf);
        return -1;
    }
}

struct gstype *
gstype_get_definition(struct gspos pos, struct gsfile_symtable *symtable, struct gstype *ty)
{
    char ty_buf[0x100];

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        gsfatal_unimpl(__FILE__, __LINE__, "buffer overflow printing type %P", ty->pos);
        return 0;
    }

    if (ty->node == gstype_abstract) {
        struct gstype_abstract *abstract;

        abstract = (struct gstype_abstract *)ty;

        return gssymtable_get_abstype(symtable, abstract->name);
    } else if (ty->node == gstype_app) {
        struct gstype_app *app;
        struct gstype *fun_defn;

        app = (struct gstype_app *)ty;

        if (fun_defn = gstype_get_definition(pos, symtable, app->fun))
            return gstype_supply(pos, fun_defn, app->arg)
        ; else
            return 0
        ;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "I don't think %s is an abstract type", ty_buf);
        return 0;
    }
}
