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
    gsinterned_string file, x, y;
    struct gstype *tyx, *tyy, *tyix, *tyiy;

    file = gsintern_string(gssymfilename, __FILE__);

    x = gsintern_string(gssymtypelable, "x");
    y = gsintern_string(gssymtypelable, "y");

    tyx = gstypes_compile_type_var(file, __LINE__, x, gskind_lifted_kind());
    tyy = gstypes_compile_type_var(file, __LINE__, y, gskind_lifted_kind());

    ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyx), "'x' is not a free variable of 'x'");
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyy), "'x' is a free variable of 'y'");

    tyix = gstypes_compile_indir(file, __LINE__, tyx);
    tyiy = gstypes_compile_indir(file, __LINE__, tyy);

    ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyix), "'x' is not a free variable of 'indir -> x'");
    not_ok(__FILE__, __LINE__, gstypes_is_ftyvar(x, tyiy), "'x' is a free variable of 'indir -> y'");
}

