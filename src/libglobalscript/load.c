#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

static gsfiletype gsreadfile(int fd, char *filename, gsheader *phdr, void **strings, void **code, void **data);
static void gsprocessstrings(void *strings, long);
static void gsprocesscode(void *strings, long, void *code, long);
static void gsprocessdata(void *strings, long, void *data, long);

typedef enum {
    unksym,
    codesym,
    datasym,
} symtype;

static void *lookup_string(void *strings, long, long sym_num, void *code, long, void *data, long, symtype *pst);
static char *lookup_string_name(void *strings, long, long sym_num);
static int gsopenfile(char *filename, int omode, int *ppid);
static int gspopen(int omode, int *ppid, char *cmd, char **argv);

gsfiletype
gsloadfile(char *filename, gsheader *phdr)
{
    int fd;
    gsfiletype res;
    void *strings, *code, *data;
    int pid;
    Waitmsg *pwait;
    if ((fd = gsopenfile(filename, OREAD, &pid)) <0)
        gsfatal("open: %r");
    if ((res = gsreadfile(fd, filename, phdr, &strings, &code, &data)) < 0)
        return res;
    gsprocessstrings(strings, phdr->strings_length);
    gsprocesscode(strings, phdr->strings_length, code, phdr->code_length);
    gsprocessdata(strings, phdr->strings_length, data, phdr->data_length);
    switch (res) {
        case gsfiledocument:
            gsfatal("%s: Create artificial thunk (or whatever) for program entry point now");
            break;
        case gsfileprefix:
            gsfatal("%s: No support for prefix files (currently)", filename);
        default:
            gsfatal("%s: Cannot provide further processing for file type %x", filename, res);
    }
    if (close(fd) < 0) gsfatal("close %s: %r", filename);
    if (pid) {
        if (!(pwait = waitfor(pid))) gsfatal("waitfor(gsbc %s): %r", filename);
        if (pwait->msg) gsfatal("%s: %s", filename, pwait->msg);
        free(pwait);
    }
    return res;
}

#define HDR_BUFFER_SIZE 0x6c
#define HDR_MIN_SIZE 0x1c

gsfiletype
gsreadfile(int fd, char *filename, gsheader *phdr, void **ppstrings, void **ppcode, void **ppdata)
{
    uchar buffer[HDR_BUFFER_SIZE];
    unsigned long length, rdlength, hdrlength;
    gsfiletype type = gsfileunknown;
    char magic[9];
    if ((length = read(fd, buffer, HDR_BUFFER_SIZE)) < 0)
        gsfatal("%s: read(header): %r", filename);
    if (length < HDR_MIN_SIZE)
        gsfatal("%s: Could not read entire header", filename);
    if (length > 2 && buffer[0] == '#' && buffer[1] == '!') {
        uchar *pb;
        for (pb = buffer; pb < buffer + length && *pb != '\n'; pb++)
            ;
        pb++; /* swallow newline */
        if (pb >= buffer + length)
            gsfatal("%s: #! line too long; could not find end");
        gswarning("%s: header offset is %x", filename, (int)(pb - buffer));
        length -= pb - buffer;
        memmove(buffer, pb, length);
    }
    if (length < 0x10) {
        if ((rdlength = read(fd, buffer + length, HDR_MIN_SIZE - length)) < 0)
            gsfatal("%s: read(header): %r", filename);
        length += rdlength;
        if (length < 0x10)
            gsfatal("%s: could not read entire buffer", filename);
    }
    hdrlength = BIG_ENDIAN_32(&buffer[0x0c]);
    gswarning("%s: Header size is %ux", filename, hdrlength);
    if (length < hdrlength) {
        if ((rdlength = read(fd, buffer + length, hdrlength - length)) < 0)
            gsfatal("%s: read(header): %r", filename);
        length += rdlength;
        if (length < hdrlength)
            gsfatal("%s: could not read entire buffer", filename);
    }
    memcpy(magic, buffer, 8);
    magic[8] = 0;
    if (!strcmp(magic, "!gsprebc"))
        type = gsfileprefix;
    else if (!strcmp(magic, "!gsdocbc"))
        type = gsfiledocument;
    else
        gsfatal("%s: bad magic: %.8s", filename, magic);
    phdr->version.era = buffer[8];
    phdr->version.major = buffer[9];
    phdr->version.minor = buffer[0xa];
    phdr->version.step = buffer[0xb];
    phdr->code_length = BIG_ENDIAN_32(&buffer[0x10]);
    phdr->data_length = BIG_ENDIAN_32(&buffer[0x14]);
    phdr->strings_length = BIG_ENDIAN_32(&buffer[0x18]);
    gswarning("%x octets of code; %x octets of data; %x octets of strings", phdr->code_length, phdr->data_length, phdr->strings_length);
    gswarning("Start of code is %x; start of data is %x; start of strings is %x", hdrlength, hdrlength + phdr->code_length, hdrlength + phdr->code_length + phdr->data_length);
    /* Code */
    *ppcode = gs_sys_input_alloc(phdr->code_length);
    if ((rdlength = read(fd, *ppcode, phdr->code_length)) < 0)
        gsfatal("%s: read(code): %r", filename);
    if (rdlength < phdr->code_length)
        gsfatal("%s: could not read entire code section into memory");
    /* Data */
    *ppdata = gs_sys_input_alloc(phdr->data_length);
    if ((rdlength = read(fd, *ppdata, phdr->data_length)) < 0)
        gsfatal("%s: read(data): %r", filename);
    if (rdlength < phdr->data_length)
        gsfatal("%s: could not read entire data section into memory; only got %x of %x octets", filename, rdlength, phdr->data_length);
    /* Strings */
    *ppstrings = gs_sys_input_alloc(phdr->strings_length);
    if (length > hdrlength)
        memcpy(*ppstrings, buffer + hdrlength, length - hdrlength);
    if ((rdlength = read(fd, *ppstrings, phdr->strings_length - (length - hdrlength))) < 0)
        gsfatal("%s: read(strings): %r", filename);
    if (rdlength < phdr->strings_length - (length - hdrlength))
        gsfatal("%s: could not read entire string section into memory; only got %x of %x octets", filename, rdlength, phdr->strings_length);
    return type;
}

void
gsprocessstrings(void *strings, long len)
{
    if (!strings)
        return;
    gsfatal("gsprocessstrings(%lx, %lx) next", (long)strings, len);
}

void
gsprocesscode(void *strings, long strlen, void *code, long len)
{
    if (!code)
        return;
    gsfatal("gsprocesscode(%lx, %lx, %lx, %lx) next", (long)strings, strlen, (long)code, len);
}

void
gsprocessdata(void *strings, long strlen, void *data, long len)
{
    if (!data)
        return;
    gsfatal("gsprocessdata(%lx, %lx, %lx, %lx) next", (long)strings, strlen, (long)data, len);
}

void *
lookup_string(void *strings, long strlen, long sym_num, void *code, long codelen, void *data, long datalen, symtype *pst)
{
    *pst = unksym;
    if (sym_num >= strlen)
        gsfatal("bad string number: %lx >= %lx", sym_num, strlen);
    gsfatal(
        "lookup_string(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx) next",
        (long)strings,
        strlen,
        sym_num,
        (long)code,
        codelen,
        (long)data,
        datalen,
        (long)pst
    );
    return 0;
}

char *
lookup_string_name(void *strings, long strlen, long sym_num)
{
    if (sym_num >= strlen)
        gsfatal("bad string number: %lx >= %lx", sym_num, strlen);
    return (char*)&((uchar*)strings)[sym_num + 0x09];
}

int
gsopenfile(char *filename, int omode, int *ppid)
{
    *ppid = 0;
    char *ext = strrchr(filename, '.');
    if (!ext) goto error;
    if (!strcmp(ext, ".bcgs"))
        return open(filename, omode);
    if (!strcmp(ext, ".ags")) {
        char *argv[] = {
            "gsbc",
            filename,
            0,
        };
        return gspopen(omode, ppid, "gsbc", argv);
    }
error:
    gsfatal("%s:extensions are mandatory in Global Script source files (sorry)", filename);
    return -1;
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
