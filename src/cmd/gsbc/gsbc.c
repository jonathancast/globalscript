#include <u.h>
#include <libc.h>
#include <bio.h>

#include "syswarning.h"

#define MAXFIELDS 256

static void bytecompile(char *filename, Biobuf *buf);

struct strcodefile
{
};

enum cmdcode {
    cmddocument,
    cmdunknown = -1,
};

static enum cmdcode cmdcode(char *filename, int line_no, char *token);

static void strcode_initialize(char *filename, struct strcodefile *file);

static void document_initialize(char *filename, struct strcodefile *file);

void
p9main(int argc, char **argv)
{
    char *filename;
    Biobuf *buf;
    ARGBEGIN {
        default:
            sysfatal("Bad option character %c", ARGC());
    } ARGEND
    if (argc) {
        while (argc) {
            filename = *argv++;
            if (!(buf = Bopen(filename, OREAD)))
                sysfatal("%s: could not open: %r", filename);
            bytecompile(filename, buf);
            if (Bterm(buf) == Beof)
                sysfatal("%s: could not close: %r", filename);
        }
    } else {
        if (!(buf = Bfdopen(0, OREAD)))
            sysfatal("/dev/stdin: could not open: %r");
        bytecompile("/dev/stdin", buf);
        if (Bterm(buf) == Beof)
            sysfatal("/dev/stdin: could not close: %r");
    }
}

void
bytecompile(char *filename, Biobuf *buf)
{
    struct strcodefile file;
    enum cmdcode code;
    int num_tokens, length;
    char *fields[MAXFIELDS];
    char *line, *s;

    strcode_initialize(filename, &file);
    while (line = Brdstr(buf, '\n', 1)) {
        file.line_no++;
        if (s = utfrune(line, '#')) *s = 0;
        length = strlen(line);
        while (length && isspace(line[length - 1])) {
            length--;
            line[length] = 0;
        }
        if (!length) continue;
        num_tokens = getfields(line, fields, MAXFIELDS, 0, "\t");
        if (num_tokens < 2)
            sysfatal("%s:%d: no command", filename, line_no);
        if (num_tokens >= MAXFIELDS)
            syswarning("%s:%d: at least %d fields, line possibly truncated", filename, line_no, MAXFIELDS);
        code = cmdcode(&file, fields[1]);
        switch (code) {
            case cmddocument:
                document_initialize(&file);
                break;
            default:
                sysfatal("%s:%d: unknown command: %s (code %d)", filename, line_no, fields[1], code);
        }
        free(line);
    }
    sysfatal("%s: bytecompile next", filename);
}

void
strcode_initialize(char *filename, struct strcodefile *file)
{
    file->filename = filename;
    file->line_no = 0;
}

void
document_initialize(struct strcodefile *file)
{
    if (file->type)
        sysfatal("%s:%d: .document not the first line in the file", file->filename, file->line_no);
    sysfatal("%s:%d: re-implement 
}

enum cmdcode
cmdcode(char *filename, int line_no, char *token)
{
    if (!strcmp(token, ".document"))
        return cmddocument;
    sysfatal("%s:%d: unknown command: %s", filename, line_no, token);
    return cmdunknown;
}
