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

gsfiletype
gsloadfile(char *filename, gsheader *phdr)
{
    int fd;
    gsfiletype res;
    void *strings, *code, *data, *tmpptr;
    symtype ty;
    if ((fd = gsopenfile(filename, OREAD)) <0)
        gsfatal("open: %r");
    if ((res = gsreadfile(fd, filename, phdr, &strings, &code, &data)) < 0)
        return res;
    gsprocessstrings(strings, phdr->strings_length);
    gsprocesscode(strings, phdr->strings_length, code, phdr->code_length);
    gsprocessdata(strings, phdr->strings_length, data, phdr->data_length);
    switch (res) {
        case gsfiledocument:
            if (!(tmpptr = lookup_string(strings, phdr->strings_length, phdr->s_entry_point, code, phdr->code_length, data, phdr->data_length, &ty)))
                gsfatal(
                    "%s: cannot find entry point named %s",
                    filename,
                    lookup_string_name(strings, phdr->strings_length, phdr->s_entry_point)
                );
            if (ty != datasym)
                gsfatal("%s: entry point not a data symbol", filename);
            phdr->entry_point = (gsvalue)tmpptr;
            break;
        default:
            gsfatal("%s: Cannot provide further processing for file type %x", filename, res);
    }
    if (close(fd) < 0) gsfatal("close: %r");
    return res;
}

#define HDR_BUFFER_SIZE 0x6c
#define HDR_MIN_SIZE 0x1c

gsfiletype
gsreadfile(int fd, char *filename, gsheader *phdr, void **ppstrings, void **ppcode, void **ppdata)
{
    uchar buffer[HDR_BUFFER_SIZE];
    long length, rdlength, hdrlength;
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
        if (pb >= buffer + length)
            gsfatal("%s: #! line too long; could not find end");
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
    phdr->strings_length = BIG_ENDIAN_32(&buffer[0x10]);
    phdr->code_length = BIG_ENDIAN_32(&buffer[0x14]);
    phdr->data_length = BIG_ENDIAN_32(&buffer[0x18]);
    switch (type) {
        case gsfiledocument:
            phdr->s_entry_point = BIG_ENDIAN_32(&buffer[0x1c]);
            break;
    }
    *ppstrings = gs_sys_input_alloc(phdr->strings_length);
    if (length > hdrlength)
        memcpy(*ppstrings, buffer + hdrlength, length - hdrlength);
    if ((rdlength = read(fd, *ppstrings, phdr->strings_length - (length - hdrlength))) < 0)
        gsfatal("%s: read(strings): %r", filename);
    if (rdlength < phdr->strings_length - (length - hdrlength))
        gsfatal("%s: could not read entire string section into memory");
    *ppcode = gs_sys_input_alloc(phdr->code_length);
    if ((rdlength = read(fd, *ppcode, phdr->code_length)) < 0)
        gsfatal("%s: read(code): %r", filename);
    if (rdlength < phdr->code_length)
        gsfatal("%s: could not read entire code section into memory");
    *ppdata = gs_sys_input_alloc(phdr->data_length);
    if ((rdlength = read(fd, *ppdata, phdr->data_length)) < 0)
        gsfatal("%s: read(data): %r", filename);
    if (rdlength < phdr->data_length)
        gsfatal("%s: could not read entire data section into memory");
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

char *lookup_string_name(void *strings, long strlen, long sym_num)
{
    if (sym_num >= strlen)
        gsfatal("bad string number: %lx >= %lx", sym_num, strlen);
    return (char*)&((uchar*)strings)[sym_num + 0x09];
}

int gsopenfile(char *filename, int omode)
{
    char *ext = strrchr(filename, '.');
    if (!ext) goto error;
    if (!strcmp(ext, ".bcgs"))
        return open(filename, omode);
    if (!strcmp(ext, ".ags")
        return gspopen(omode, "gsbc", filename, 0);
error:
    gsfatal("%s:extensions are mandatory in Global Script source files (sorry)", filename);
}

int gspopen(int omode, char *cmd, ...)
{
    va_list arg;

    gsfatal("gspopen(%d, %s) next", omode, cmd);
}
