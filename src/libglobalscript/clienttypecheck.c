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

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %s:%d", __FILE__, __LINE__, ty->file->name, ty->lineno);
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

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %s:%d", __FILE__, __LINE__, ty->file->name, ty->lineno);
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
    } else {
        seprint(err, eerr, "I don't think %s is a primitive type", ty_buf);
        return -1;
    }
}

int
gstype_expect_lift(struct gstype *ty, struct gstype **tyarg, char *err, char *eerr)
{
    char ty_buf[0x100];

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %s:%d", __FILE__, __LINE__, ty->file->name, ty->lineno);
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

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        seprint(err, eerr, "%s:%d: buffer overflow printing type %s:%d", __FILE__, __LINE__, ty->file->name, ty->lineno);
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
