#include <u.h>
#include <libc.h>
#include <libtest.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gstypealloc.h"

#include "test_tests.h"

static void TEST_EXPECT_PRODUCT(void);

void
test_clienttypecheck()
{
    RUNTESTS(TEST_EXPECT_PRODUCT);
}

static
void
TEST_EXPECT_PRODUCT()
{
    char err[0x100];
    struct gspos pos;
    struct gstype *tyx, *tyerec;

    pos.file = gsintern_string(gssymfilename, __FILE__);

    pos.lineno = __LINE__; tyx = gstypes_compile_type_var(pos, gsintern_string(gssymtypelable, "x"), gskind_lifted_kind());
    pos.lineno = __LINE__; tyerec = gstypes_compile_product(pos, 0);

    ok_long_lt(__FILE__, __LINE__, gstype_expect_product(err, err + sizeof(err), tyx, 0), 0, "gstype_expect_product succeeded on a var");
    ok_long_eq(__FILE__, __LINE__, gstype_expect_product(err, err + sizeof(err), tyerec, 0), 0, "gstype_expect_product failed on an empty record: %s", err);
}
