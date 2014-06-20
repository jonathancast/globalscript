#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#ifdef SHOULD_DEBUG
int gsdebug = SHOULD_DEBUG;
#else
int gsdebug = 1;
#endif

/* Based on p9p's sysfatal function */
void
gsfatal(char *err, ...)
{
    char buf[0x400];
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

    if (gsdebug) {
        fprint(2, "%s: %s:%d: %s\n", argv0, srcfile, srcline, buf);
    } else {
        fprint(2, "%s: %s\n", argv0, buf);
    }
    exits("asssert failed");
}

void
gswerrstr_unimpl(char *file, int lineno, char *err, ...)
{
    char buf[0x100];
    va_list arg;

    va_start(arg, err);
    vseprint(buf, buf+sizeof buf, err, arg);
    va_end(arg);

    if (gsdebug)
        werrstr("%s:%d: unimpl %s", file, lineno, buf)
    ; else
        werrstr("unimpl %s", buf)
    ;
}
