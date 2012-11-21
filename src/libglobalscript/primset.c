/* §source.file{Known primsets} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gssetup.h"
#include "gsregtables.h"

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

static struct gsregistered_primtype rune_prim_types[] = {
    /* name, file, line, group, kind, */
    { "rune", __FILE__, __LINE__, gsprim_type_defined, "u", },
    { 0, },
};

static gsubprim_handler rune_prim_handle_eq;

enum {
    rune_prim_ub_eq,
};

static gsubprim_handler *rune_prim_ubexec[] = {
    rune_prim_handle_eq,
};

static struct gsregistered_prim rune_prim_operations[] = {
    /* name, file, line, group, apitype, type, index, */
    { "eq", __FILE__, __LINE__, gsprim_operation_unboxed, 0, "rune.prim.rune rune.prim.rune \"uΠ〈 〉 \"uΠ〈 〉 \"uΣ〈 0 1 〉 → →", rune_prim_ub_eq, },
    { 0, },
};

static struct gsregistered_primset rune_prim_set = {
    /* name = */ "rune.prim",
    /* types = */ rune_prim_types,
    /* operations = */ rune_prim_operations,
    /* ubexec_table = */ rune_prim_ubexec,
};

void
gsadd_global_script_prim_sets()
{
    gsprims_register_prim_set(&rune_prim_set);
}

static
int
rune_prim_handle_eq(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args)
{
    if (args[0] != args[1])
        return gsubprim_return(thread, pos, 0, 0)
    ; else
        return gsubprim_return(thread, pos, 1, 0)
    ;
}
