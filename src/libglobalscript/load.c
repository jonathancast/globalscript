/* §source.file{Loading String Code Files & Other Source Files */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

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

static gsparsedfile *gsreadfile(char *filename, char *relname, int skip_docs, struct gsfile_symtable *symtable);

struct gsfile_symtable *gscurrent_symtable;

gsfiletype
gsloadfile(char *filename, gsvalue *pentry)
{
    gsparsedfile *parsedfile;
    struct gsfile_symtable *symtable;

    symtable = gscreatesymtable(gscurrent_symtable);

    parsedfile = gsreadfile(filename, "", 0, symtable);

    return parsedfile->type;
}

static long gsgrabline(char *filename, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_data_item(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);

#define LINE_LENGTH 0x100
#define NUM_FIELDS 0x20

gsparsedfile *
gsreadfile(char *filename, char *relname, int skip_docs, struct gsfile_symtable *symtable)
{
    struct uxio_ichannel *chan;
    char line[LINE_LENGTH];
    char *fields[NUM_FIELDS];
    gsparsedfile *parsedfile;
    struct gsparsedfile_segment *pseg;
    int pid;
    long n;
    gsfiletype type;
    int lineno;
    enum {
        gsnosection,
        gsdatasection,
        gscodesection,
    } section;

    if (!(chan = gsopenfile(filename, OREAD, &pid)))
        gsfatal("open: %r");

    lineno = 0;

    if ((n = gsgrabline(filename, chan, line, &lineno, fields)) < 0)
        gsfatal("%s: Error in reading line: %r", filename);
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
    if ((parsedfile = gsparsed_file_alloc(filename, relname, type)) < 0) {
        gsfatal("%s:%d: Cannot allocate input file: %r", filename, lineno);
    }
    pseg = &parsedfile->first_seg;

    section = gsnosection;

    /*
        We read in the first line of each item and pass it to the appropriate parse function.
        The parse function is allowed to read in more lines if it needs them.
        It is always un-ambiguous whether more lines are needed;
        so when the parse function returns, we are ready to read in the next item (or section declaration).
    */
    while ((n = gsgrabline(filename, chan, line, &lineno, fields)) > 0) {
        if (n < 2) gsfatal("%s:%d: Missing directive", filename, lineno);
        if (!strcmp(fields[1], ".data")) {
            section = gsdatasection;
            parsedfile->data = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->data), &pseg);
            parsedfile->data->numitems = 0;
        } else if (!strcmp(fields[1], ".code")) {
            section = gscodesection;
            parsedfile->code = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->code), &pseg);
            parsedfile->code->numitems = 0;
        }
        else switch (section) {
            case gsnosection:
                gsfatal("%s:%d: Missing section directive", filename, lineno);
                break;
            case gsdatasection:
                if (gsparse_data_item(filename, parsedfile, &pseg, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading data item: %r", filename, lineno);
                break;
            case gscodesection:
                if (gsparse_code_item(filename, parsedfile, &pseg, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading code item: %r", filename, lineno);
                break;
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading data item: %r", filename, lineno);

    if (!ibio_idevice_at_eof(chan))
        gsfatal("%s:%d: Expected EOF", filename, lineno);

    if (gsclosefile(chan, pid) < 0)
        gsfatal("%s: Error in closing file: %r", filename);

    return parsedfile;
}

enum gsdatadirective {
    gsclosuredirective,
    gsnumdatadirectives,
};

static gsinterned_string interned_data_directives[gsnumdatadirectives];
static void gsparse_data_initialize_interned_data_directives(void);

static char *data_directive_names[] = {
    ".closure",
};

static
long
gsparse_data_item(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;
    int i;
    enum gsdatadirective directive;

    parsedline = gsparsed_file_addline(filename, parsedfile, ppseg, *plineno, numfields);
    parsedfile->data->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymdatalable, fields[0]);
    else
        parsedline->label = 0;

    gssymtable_add_data_item(symtable, parsedline->label, parsedfile, parsedline);

    if (!interned_data_directives[0])
        gsparse_data_initialize_interned_data_directives();

    parsedline->directive = gsintern_string(gssymdatadirective, fields[1]);

    for (i = 0; i < gsnumdatadirectives; i++)
        if (parsedline->directive == interned_data_directives[i]) {
            directive = i;
            goto found_directive;
        }
    
    gsfatal("%s:%d: Unrecognized data directive %s", filename, *plineno, fields[1]);

found_directive:    

    switch (directive) {
        case gsclosuredirective:
            if (numfields < 2+0+1)
                gsfatal("%s:%d: Missing code label", filename, *plineno);
            parsedline->arguments[0] = gsintern_string(gssymcodelable, fields[2+0]);
            if (numfields >= 2+1+1)
                parsedline->arguments[1] = gsintern_string(gssymtypelable, fields[2+1]);
            if (numfields > 2+1+1)
                gsfatal("%s:%d: Data item %s has too many arguments; I don't know what they all are", filename, *plineno, fields[0]);
            break;
        default:
            gsfatal("%s:%d: Unimplemented data directive %s", filename, *plineno, fields[1]);
    }

    return 1;
}

static
void
gsparse_data_initialize_interned_data_directives()
{
    int i;

    for (i = 0; i < gsnumdatadirectives; i++)
        interned_data_directives[i] = gsintern_string(gssymdatadirective, data_directive_names[i]);
}

enum gscodedirective {
    gsexprdirective,
    gsnumcodedirectives,
};

static gsinterned_string interned_code_directives[gsnumcodedirectives];
static void gsparse_code_initialize_interned_code_directives(void);

static char *code_directive_names[] = {
    ".expr",
};

enum gscodeop {
    gsgvarop,
    gscallop,
    gsnumcodeops,
};

static gsinterned_string interned_code_ops[gsnumcodeops];

static void gsparse_code_initialize_interned_code_ops(void);

static char *code_op_names[] = {
    ".gvar",
    ".call",
};

static long gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);

static
long
gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;
    int i;
    enum gscodedirective directive;

    parsedline = gsparsed_file_addline(filename, parsedfile, ppseg, *plineno, numfields);
    parsedfile->code->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymcodelable, fields[0]);
    else
        gsfatal("%s:%d: Missing code label", filename, *plineno);

    gssymtable_add_code_item(symtable, parsedline->label, parsedfile, parsedline);

    if (!interned_code_directives[0])
        gsparse_code_initialize_interned_code_directives();

    parsedline->directive = gsintern_string(gssymcodedirective, fields[1]);

    for (i = 0; i < gsnumcodedirectives; i++)
        if (parsedline->directive == interned_code_directives[i]) {
            directive = i;
            goto found_directive;
        }

    gsfatal("%s:%d: Unrecognized code directive %s", filename, *plineno, fields[1]);

found_directive:

    switch (directive) {
        case gsexprdirective:
            return gsparse_code_ops(filename, parsedfile, ppseg, parsedline, chan, line, plineno, fields);
        default:
            gsfatal("%s:%d: Unimplemented code directive %s", filename, *plineno, fields[1]);
    }

    gsfatal("%s:%d: gsparse_code_item next", __FILE__, __LINE__);

    return -1;
}

static
long
gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        enum gscodeop op;

        parsedline = gsparsed_file_addline(filename, parsedfile, ppseg, *plineno, n);

        if (!interned_code_ops[0])
            gsparse_code_initialize_interned_code_ops();

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        for (i = 0; i < gsnumcodeops; i++)
            if (interned_code_ops[i] == parsedline->directive) {
                op = i;
                goto found_op;
            }

        gsfatal("%s:%d: Un-recognized code op %s", filename, *plineno, fields[1]);

    found_op:
        switch (op) {
            case gsgvarop:
                if (*fields[0])
                    parsedline->label = gsintern_string(gssymtypelable, fields[0]);
                else
                    gsfatal("%s:%d: Missing label on .gvar op", filename, *plineno);
                if (n > 2)
                    gsfatal("%s:%d: Too many arguments to .gvar op", filename, *plineno);
                break;
            case gscallop:
                if (*fields[0])
                    gsfatal("%s:%d: Labels not allowed on terminal declarations", filename, *plineno);
                else
                    parsedline->label = 0;
                if (n < 4)
                    gsfatal("%s:%d: Not enough arguments to .call", filename, *plineno);
                for (i = 2; i < n; i++)
                    parsedline->arguments[i - 2] = gsintern_string(gssymreglable, fields[i]);
                return 0;
            default:
                gsfatal("%s:%d: Unimplemented code op %s", filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading code line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading expression", filename, codedirective->lineno);

    return -1;
}

static
void
gsparse_code_initialize_interned_code_directives()
{
    int i;

    for (i = 0; i < gsnumcodedirectives; i++)
        interned_code_directives[i] = gsintern_string(gssymcodedirective, code_directive_names[i]);
}

static
void
gsparse_code_initialize_interned_code_ops()
{
    int i;

    for (i = 0; i < gsnumcodeops; i++)
        interned_code_ops[i] = gsintern_string(gssymcodeop, code_op_names[i]);
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

struct gsfile_symtable {
    struct gsfile_symtable *parent;
    struct gsfile_symtable_item *dataitems, *codeitems, *typeitems;
};

/* NB: linear-time! */

struct gsfile_symtable_item {
    gsinterned_string key;
    gsparsedfile *file;
    struct gsparsedline *value;
    struct gsfile_symtable_item *next;
};

static struct gs_block_class gssymtable_segment = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol tables",
};

static void *symtable_nursury;

static struct gs_block_class gssymtable_item_segment = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol table entries",
};

static void *symtable_item_nursury;

struct gsfile_symtable *
gscreatesymtable(struct gsfile_symtable *prev_symtable)
{
    struct gsfile_symtable *newsymtable;

    newsymtable = gs_sys_seg_suballoc(&gssymtable_segment, &symtable_nursury, sizeof(*newsymtable), sizeof(void*));

    newsymtable->parent = prev_symtable;
    newsymtable->dataitems = 0;
    newsymtable->codeitems = 0;
    newsymtable->typeitems = 0;

    return newsymtable;
}

void
gssymtable_add_code_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedline *pcode)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->codeitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate code item %s (duplicate of %s:%d)",
                pcode->file->name,
                pcode->lineno,
                label->name,
                (*p)->value->file->name,
                (*p)->value->lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->value = pcode;
    (*p)->next = 0;
}

void
gssymtable_add_data_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedline *pdata)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->dataitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate data item %s (duplicate of %s:%d)",
                pdata->file->name,
                pdata->lineno,
                label->name,
                (*p)->value->file->name,
                (*p)->value->lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->value = pdata;
    (*p)->next = 0;
}

struct gsbc_item
gssymtable_lookup(char *filename, int lineno, struct gsfile_symtable *symtable, gsinterned_string label)
{
    struct gsbc_item res;
    char *strtype;
    struct gsfile_symtable_item *p;

    gsbc_item_empty(&res);

    while (symtable) {
        switch (label->type) {
            case gssymdatalable:
                for (p = symtable->dataitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymdatalable;
                        res.v.pdata = p->value;
                        return res;
                    }
                }
                break;
            case gssymcodelable:
                for (p = symtable->codeitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymcodelable;
                        res.v.pcode = p->value;
                        return res;
                    }
                }
                break;
            case gssymtypelable:
                for (p = symtable->typeitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymtypelable;
                        res.v.ptype = p->value;
                        return res;
                    }
                }
                break;
            default:
                gsfatal("%s:%d: Unknown symbol type %d", __FILE__, __LINE__, label->type);
        }
        symtable = symtable->parent;
    }

    strtype = 0;

    switch (label->type) {
        case gssymdatalable:
            strtype = "data label";
            break;
        case gssymcodelable:
            strtype = "code label";
            break;
        case gssymtypelable:
            strtype = "type label";
            break;
        default:
            gsfatal("%s:%d: Cannot translate symbol type %d to a string", __FILE__, __LINE__, label->type);
    }

    gsfatal("%s:%d: Unknown %s '%s'", filename, lineno, strtype, label->name);

    return res;
}

struct uxio_ichannel *
gsopenfile(char *filename, int omode, int *ppid)
{
    char *ext;

    *ppid = 0;
    ext = strrchr(filename, '.');
    if (!ext) goto error;
    if (!strcmp(ext, ".ags") || !strcmp(ext, ".gsac")) {
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