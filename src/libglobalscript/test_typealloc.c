#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"

#include "test_tests.h"

static void TEST_FV_VAR(void);

void
test_typealloc()
{
    RUNTESTS(TEST_FV_VAR);
}

static
void
TEST_FV_VAR()
{
    gsinterned_string file, x, y, ux, uy;
    struct gspos pos;
    struct gstype *tyx, *tyy, *tyix, *tyiy, *tyux, *tyuy, *tylux, *tyluy, *tyes;

    file = gsintern_string(gssymfilename, __FILE__);
    pos.file = file;

    x = gsintern_string(gssymtypelable, "x");
    y = gsintern_string(gssymtypelable, "y");

    pos.lineno = __LINE__; tyx = gstypes_compile_type_var(pos, x, gskind_lifted_kind());
    pos.lineno = __LINE__; tyy = gstypes_compile_type_var(pos, y, gskind_lifted_kind());

    ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyx), "'x' is not a free variable of 'x'");
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyy), "'x' is a free variable of 'y'");

    pos.lineno = __LINE__; tyix = gstypes_compile_indir(pos, tyx);
    pos.lineno = __LINE__; tyiy = gstypes_compile_indir(pos, tyy);

    ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyix), "'x' is not a free variable of 'indir -> x'");
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyiy), "'x' is a free variable of 'indir -> y'");

    ux = gsintern_string(gssymtypelable, "ux");
    uy = gsintern_string(gssymtypelable, "uy");

    pos.lineno = __LINE__; tyux = gstypes_compile_type_var(pos, ux, gskind_unlifted_kind());
    pos.lineno = __LINE__; tyuy = gstypes_compile_type_var(pos, uy, gskind_unlifted_kind());

    pos.lineno = __LINE__; tylux = gstypes_compile_lift(pos, tyux);
    pos.lineno = __LINE__; tyluy = gstypes_compile_lift(pos, tyuy);

    ok(__FILE__, __LINE__, gstypes_is_ftyvar(ux, tylux), "'ux' is not a free variable of '⌊ux⌋'");
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(ux, tyluy), "'ux' is a free variable of '⌊uy⌋'");

    tyes = gstypes_compile_sum(file, __LINE__, 0);
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyes), "'x' is a free type variable of Σ〈〉");
}

