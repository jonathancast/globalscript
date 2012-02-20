#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#ifdef SHOULD_DEBUG
int debug = SHOULD_DEBUG;
#else
int debug = 1;
#endif

/* Based on p9p's sysfatal function */
void
gsfatal(char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    fprint(2, "%s: %s\n", argv0, buf);
    exits("fatal");
}

void
gswarning(char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    fprint(2, "%s: %s\n", argv0, buf);
}

void
gsassert(char *srcfile, int srcline, int success, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    if (success) return;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    if (debug) {
        fprint(2, "%s:%s:%d %s\n", argv0, srcfile, srcline, buf);
    } else {
        fprint(2, "%s: %s\n", argv0, buf);
    }
    exits("asssert failed");
}

void
gsdeny(char *srcfile, int srcline, int success, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    gsassert(srcfile, srcline, !success, "%s", buf);
}

void
gsassert_ulong_eq(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    gsassert(srcfile, srcline, n0 == n1, "Values differ: %ux != %ux: %s", n0, n1, buf);
}

void
gsassert_ulong_le(char *srcfile, int srcline, ulong n0, ulong n1, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    gsassert(srcfile, srcline, n0 <= n1, "Values inconsistent: %ux !<= %ux: %s", n0, n1, buf);
}