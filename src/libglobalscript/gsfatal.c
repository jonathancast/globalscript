#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#ifdef SHOULD_DEBUG
static int debug = SHOULD_DEBUG;
#else
static int debug = 1;
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
        fprint(2, "%s: %s:%d: %s\n", argv0, srcfile, srcline, buf);
    } else {
        fprint(2, "%s: %s\n", argv0, buf);
    }
    exits("asssert failed");
}

void
gsfatal_unimpl(char *file, int lineno, char * err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    if (debug)
        gsfatal("%s:%d: %s next", file, lineno, buf)
    ; else
        gsfatal("Panic: Un-implemented operation in release build: %s", buf)
    ;
}