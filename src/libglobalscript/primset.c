/* §source.file{Known primsets} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gssetup.h"

static struct gsregistered_primset *registered_primsets[MAX_NUM_REGISTERS];
static int num_registered_primsets;

void
gsprims_register_prim_set(struct gsregistered_primset *value)
{
    if (num_registered_primsets >= MAX_NUM_REGISTERS)
        gsfatal(UNIMPL("Can only register up to 0x%x primsets"), MAX_NUM_REGISTERS)
    ;
    registered_primsets[num_registered_primsets++] = value;
}

struct gsregistered_primset *
gsprims_lookup_prim_set(char *name)
{
    int i;

    for (i = 0; i < num_registered_primsets; i++) {
        if (!strcmp(registered_primsets[i]->name, name))
            return registered_primsets[i]
        ;
    }

    return 0;
}

struct gsregistered_primset *
gsprims_lookup_prim_set_by_index(int i)
{
    return registered_primsets[i];
}

int
gsprims_prim_set_index(struct gsregistered_primset *prims)
{
    int i;

    for (i = 0; i < num_registered_primsets; i++) {
        if (registered_primsets[i] == prims)
            return i
        ;
    }

    return -1;
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

void
gsadd_global_script_prim_sets()
{
    gsprims_register_prim_set(&gsrune_prim_set);
    gsprims_register_prim_set(&gsnatural_prim_set);
}
