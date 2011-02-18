#include <lib9.h>

#include "syswarning.h"

void
syswarning(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);

	__fixargv0();
	fprint(2, "%s: %s\n", argv0 ? argv0 : "<prog>", buf);
}
