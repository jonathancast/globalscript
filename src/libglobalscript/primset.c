/* §source.file{Known primsets} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gssetup.h"

struct gsregistered_primset_link {
    gsinterned_string key;
    struct gsregistered_primset *value;
    struct gsregistered_primset_link *next;
};

static struct gsregistered_primset_link *registered_primsets;

static struct gs_block_class gsregistered_primset_link_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Registered primitive set descriptor links",
};
static void *gsregistered_primset_link_nursury;

void
gsprims_register_prim_set(struct gsregistered_primset *value)
{
    struct gsregistered_primset_link **p;
    gsinterned_string interned_name;

    interned_name = gsintern_string(gssymprimsetlable, value->name);

    for (p = &registered_primsets; *p; p = &(*p)->next) {
        if ((*p)->key == interned_name)
            gsfatal("%s: Duplicate registered primset", value->name);
    }

    *p = gs_sys_seg_suballoc(&gsregistered_primset_link_descr, &gsregistered_primset_link_nursury, sizeof(**p), sizeof(void*));

    (*p)->key = interned_name;
    (*p)->value = value;
    (*p)->next = 0;
}

struct gsregistered_primset *
gsprims_lookup_prim_set(char *name)
{
    struct gsregistered_primset_link *p;
    gsinterned_string interned_name;

    interned_name = gsintern_string(gssymprimsetlable, name);

    for (p = registered_primsets; p; p = p->next) {
        if (p->key == interned_name)
            return p->value
        ;
    }

    return 0;
}

struct gsregistered_primtype *
gsprims_lookup_type(struct gsregistered_primset *prims, char *name)
{
    struct gsregistered_primtype *p;

    for (p = prims->types; p->name; p++) {
        if (!strcmp(p->name, name))
            return p;
    }

    return 0;
}

struct gsregistered_prim *
gsprims_lookup_prim(struct gsregistered_primset *prims, char *name)
{
    struct gsregistered_prim *p;

    if (!prims->operations)
        return 0
    ;

    for (p = prims->operations; p->name; p++) {
        if (!strcmp(p->name, name))
            return p;
    }

    return 0;
}

static struct gsregistered_primtype rune_prim_types[] = {
    /* name, file, line, group, kind, */
    { "rune", __FILE__, __LINE__, gsprim_type_defined, "u", },
    { 0, 0, },
};

static struct gsregistered_primset rune_prim_set = {
    /* name = */ "rune.prim",
    /* types = */ rune_prim_types,
};

void
gsadd_global_script_prim_sets()
{
    gsprims_register_prim_set(&rune_prim_set);
}

