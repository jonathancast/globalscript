#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

#define LINE_LENGTH 0x100
#define NUM_FIELDS 0x20

typedef enum {
    unksym,
    codesym,
    datasym,
} symtype;

static struct uxio_ichannel *gsopenfile(char *filename, int omode, int *ppid);
static struct uxio_channel *gspopen(int omode, int *ppid, char *cmd, char **argv);
static long gsclosefile(struct uxio_ichannel *chan, int pid);

static long gsac_tokenize(char *, char **, long);

int
gsisdir(char *filename)
{
    struct ibio_dir *d;
    d = ibio_stat(filename);
    return d->d.mode & DMDIR;
}

void
gsadddir(char *filename)
{
}

static long gsgrabline(char *filename, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);

gsfiletype
gsloadfile(char *filename, gsinputheader *phdr, gsvalue *pentry)
{
    struct uxio_ichannel *chan;
    int pid;
    char line[LINE_LENGTH];
    char *fields[NUM_FIELDS];
    long n;
    int lineno;
    gsfiletype type;

    if (!(chan = gsopenfile(filename, OREAD, &pid)))
        gsfatal("open: %r");

    lineno = 0;

    if ((n = gsgrabline(filename, chan, line, &lineno, fields)) < 0)
        gsfatal("%s: Error in reading ilne: %r", filename);
    if (n == 0) {
        if (lineno <= 1)
            gsfatal("%s: EOF before reading first line", filename);
        else
            gsfatal("%s:%d: EOF before reading file directive", filename, lineno);
    }
    if (!strcmp(fields[1], ".document")) {
        type = gsfiledocument;
    } else {
        gsfatal("%s:%d: Cannot understand directive '%s'", filename, lineno, fields[1]);
    }

    while ((n = gsgrabline(filename, chan, line, &lineno, fields)) > 0);
    if (n < 0)
        gsfatal("%s:%d: Error in reading line: %r", filename, lineno);

    if (gsclosefile(chan, pid) < 0)
        gsfatal("%s: Error in closing file: %r", filename);

    return type;
}

static
long
gsgrabline(char *filename, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    long n;

    for (;;) {
        (*plineno)++;
        if ((n = ibio_device_getline(chan, line, LINE_LENGTH)) < 0)
            gsfatal("%s: getline: %r", filename);
        if (n == LINE_LENGTH)
            gsfatal("%s:%d: line too long; max length %d", filename, *plineno, LINE_LENGTH - 2);
        if (n <= 1)
            return 0;
        if ((n = gsac_tokenize(line, fields, NUM_FIELDS)) < 0)
            gsfatal("%s:%d: Fatal error in lexer: %r", filename, *plineno);
        if (n > NUM_FIELDS)
            gsfatal("%s:%d: Too many fields; max is %d", filename, *plineno, NUM_FIELDS - 1);
        if (n == 0)
            continue;
        if (n == 1)
            gsfatal("%s:%d: Missing directive", filename, *plineno);
        return n;
    }
}

struct uxio_ichannel *
gsopenfile(char *filename, int omode, int *ppid)
{
    char *ext;

    *ppid = 0;
    ext = strrchr(filename, '.');
    if (!ext) goto error;
    if (!strcmp(ext, ".ags")) {
        return ibio_device_iopen(filename, omode);
    }
error:
    gsfatal("%s:extensions are mandatory in Global Script source files (sorry)", filename);
    return 0;
}

static
long
gsclosefile(struct uxio_ichannel *chan, int pid)
{
    if (pid)
        gsfatal("gsclosefile for pipe next");

    return ibio_device_iclose(chan);
}

static
long
gsac_tokenize(char *line, char **fields, long maxfields)
{
    int label_present;
    long numfields;
    char *p;
    char **pfield;

    numfields = 0;
    p = line;
    fields[0] = line;
    while (*p && !isspace(*p) && *p != '#')
        p++;
    label_present = p > line;
    if (*p)
        *p++ = 0;

    pfield = fields + 1;
    while (*p && *p != '#' && pfield < fields + maxfields) {
        while (*p && isspace(*p) && *p != '#')
            p++;
        if (*p && *p != '#') {
            *pfield++ = p;
            while (*p && !isspace(*p) && *p != '#')
                p++;
            if (*p)
                *p++ = 0;
            numfields++;
        }
    }
    return label_present || numfields ? numfields + 1 : 0;
}