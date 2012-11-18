/* §source.file Client-level Types */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

struct gstype *
gstypes_compile_rune(struct gspos pos)
{
    return gstypes_compile_abstract(pos, gsintern_string(gssymtypelable, "rune.t"), gskind_compile_string(pos, "*"));
}
