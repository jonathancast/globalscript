/* §source.file Register table manipulation */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
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
    gsfatal("%P: No such register '%y'", p->pos, name);
    return -1;
}
