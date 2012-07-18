#include <u.h>
#include <libc.h>
#include <libtest.h>

struct teardown_closure {
    void (*callback)(struct teardown_closure *);
};

#define MAXTEARDOWNS 50
static struct teardown_closure *teardowns[MAXTEARDOWNS];
static int num_teardowns;

void start_tests()
{
    atexit(test_teardown);
}

void test_teardown()
{
    while (num_teardowns) {
        (*teardowns[num_teardowns]->callback)(teardowns[num_teardowns]);
        num_teardowns--;
    }
}

void
ok(char *srcfile, int srcline, int success, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    if (success) return;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    fprint(2, "%s:%d: %s\n", srcfile, srcline, buf);
    exits("ok failed");
}

void
not_ok(char *srcfile, int srcline, int success, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, !success, "%s", buf);
}

void
ok_ulong_eq(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, n0 == n1, "%s: %ux != %ux", buf, n0, n1);
}

void
ok_ulong_ne(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, n0 != n1, "%s: %ux == %ux", buf, n0, n1);
}

void
ok_ulong_le(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, n0 <= n1, "%s: %ux !<= %ux", buf, n0, n1);
}

void
ok_ulong_ge(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, n0 >= n1, "%s: %ux !>= %ux", buf, n0, n1);
}

void
ok_cstring_eq(char *srcfile, int srcline, char *s0, char *s1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    ok(srcfile, srcline, !strcmp(s0, s1), "%s: '%s' != '%s'", buf, s0, s1);
}
