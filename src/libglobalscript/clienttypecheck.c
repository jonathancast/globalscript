/* §source.file Client-level Type Checker */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"
#include "gstypecheck.h"

int
gstype_expect_abstract(struct gstype *ty, char *name)
{
    char ty_buf[0x100];

    ty = gstypes_clear_indirections(ty);

    if (gstypes_eprint_type(ty_buf, ty_buf + sizeof(ty_buf), ty) >= ty_buf + sizeof(ty_buf)) {
        werrstr("%s:%d: buffer overflow printing type %s:%d", __FILE__, __LINE__, ty->file->name, ty->lineno);
        return -1;
    }

    if (ty->node == gstype_abstract) {
        struct gstype_abstract *abstract;

        abstract = (struct gstype_abstract *)ty;

        if (strcmp(abstract->name->name, name)) {
            werrstr("I don't think %s is %s", abstract->name->name, name);
            return -1;
        } else {
            return 0;
        }
    } else {
        werrstr("I don't think %s is an abstract type", ty_buf);
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
