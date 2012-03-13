#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

typedef enum {
    unksym,
    codesym,
    datasym,
} symtype;

static int gspopen(int omode, int *ppid, char *cmd, char **argv);

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

gsfiletype
gsloadfile(char *filename, gsinputheader *phdr, gsvalue *pentry)
{
    gsfatal("gsloadfile(%s) next", filename);
    return gsfileerror;
}

int
gspopen(int omode, int *ppid, char *cmd, char **argv)
{
    int fd[2];
    if (pipe(fd) < 0)
        return -1;
    if ((*ppid = rfork(RFPROC | RFFDG)) < 0) {
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (*ppid == 0) {
        switch (omode) {
        case OREAD:
            close(1);
            dup(fd[1], 1);
            exec(cmd, argv);
            exits("exec failed");
        default:
            gsfatal("gspopen(%d, %s, argv) next", omode, cmd);
        }
        exits("shouldn't have gotten here");
    }
    close(fd[1]);
    return fd[0];
}

gscode
gsloadcodeobject(void *pcode)
{
    gsfatal("gsloadcodeobject(pcode) next");
    return 0;
}
