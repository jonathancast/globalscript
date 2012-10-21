/* §source.file Register table manipulation */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsregtables.h"

int
gsbc_find_register(struct gsparsedline *p, gsinterned_string *regs, int nregs, gsinterned_string name)
{
    int i;

    for (i = 0; i < nregs; i++) {
        if (regs[i] == name)
            return i;
    }
    gsfatal_bad_input(p, "No such register '%s'", name->name);
    return -1;
}
