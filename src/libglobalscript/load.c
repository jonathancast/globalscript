/* §source.file{Loading String Code Files & Other Source Files} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include <libibio.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"
#include "gsbytecompile.h"
#include "gstopsort.h"
#include "gstypealloc.h"
#include "gstypecheck.h"
#include "ace.h"

static struct uxio_ichannel *gsopenfile(char *filename, int omode, int *ppid);
#if 0
static struct uxio_channel *gspopen(int omode, int *ppid, char *cmd, char **argv);
#endif
static long gsclosefile(struct uxio_ichannel *chan, int pid);

static long gsac_tokenize(char *, char **, long);

int
gsisdir(char *filename)
{
    struct ibio_dir *d;
    d = ibio_stat(filename);
    return d->d.mode & DMDIR;
}

#define FILE_NAME_SIZE_LIMIT 0x100

void
gsadd_global_gslib()
{
    struct uxio_ichannel *chan;
    char globalscript[FILE_NAME_SIZE_LIMIT], buf[FILE_NAME_SIZE_LIMIT];
    long n;

    if (!(chan = ibio_envvar_iopen("GLOBALSCRIPT"))) {
        strncpy(globalscript, "", FILE_NAME_SIZE_LIMIT);
        goto set_buf;
    }
    if ((n = ibio_get_contents(chan, globalscript, FILE_NAME_SIZE_LIMIT)) < 0)
        gsfatal("Error reading $GLOBALSCRIPT: %r")
    ;
    if (n >= FILE_NAME_SIZE_LIMIT) {
        globalscript[n - 1] = 0;
        gsfatal("%s:%d: $GLOBALSCRIPT too large (only read %s)", __FILE__, __LINE__, globalscript);
    }
    if (ibio_device_iclose(chan) < 0)
        gsfatal("Could not close $GLOBALSCRIPT fd: %r");

set_buf:
    if ((n = snprint(buf, FILE_NAME_SIZE_LIMIT, "%s/gslib", globalscript)) < 0)
        gsfatal("Error formatting global directory name: %r")
    ;
    if (n >= FILE_NAME_SIZE_LIMIT)
        gsfatal("%s:%d: Overflow formatting global directory name; $GLOBALSCRIPT (=%s) is too large",
            __FILE__,
            __LINE__,
            globalscript
        )
    ;

    gsadddir(buf);
}

static void gsloadfile(gsparsedfile *, struct gsfile_symtable *, gsvalue *, struct gstype **);

struct gsfile_link {
    gsparsedfile *file;
    struct gsfile_link *next;
};

static void gsaddir_recursive(char *filename, char *relname, struct gsfile_symtable *symtable, struct gsfile_link ***);

void
gsadddir(char *filename)
{
    struct gsfile_symtable *symtable;
    struct gsfile_link *pfile, *file_list, **pend;

    symtable = gscreatesymtable(gscurrent_symtable);

    file_list = 0;
    pend = &file_list;

    gsaddir_recursive(filename, "", symtable, &pend);

    for (pfile = file_list; pfile; pfile = pfile->next) {
        gsloadfile(pfile->file, symtable, 0, 0);
    }

    gscurrent_symtable = symtable;
}

static gsparsedfile *gsreadfile(char *filename, char *relname, int skip_docs, int *, struct gsfile_symtable *symtable);

static struct gs_block_class filename_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Filenames for gsaddir_recursive",
};
static void *filename_nursury;

static struct gs_block_class file_link_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "File list links for returning file list to gsaddir",
};
static void *file_link_nursury;

static
void
gsaddir_recursive(char *filename, char *relname, struct gsfile_symtable *symtable, struct gsfile_link ***ppend)
{
    struct uxio_dir_ichannel *chan;
    struct ibio_dir *dir;

    if (!(chan = ibio_dir_iopen(filename, OREAD)))
        gsfatal("open: %r")
    ;

    while (dir = ibio_read_stat(chan)) {
        char *newfilename, *newrelname;
        char *ext;
        struct gsfile_symtable *file_symtable;
        gsparsedfile *file;

        if (!strcmp(dir->d.name, "."))
            continue;
        if (!strcmp(dir->d.name, ".."))
            continue;

        newfilename = gs_sys_seg_suballoc(
            &filename_desc,
            &filename_nursury,
            strlen(filename) + 1 + strlen(dir->d.name) + 1,
            1
        );
        sprint(newfilename, "%s/%s", filename, dir->d.name);
        newrelname = gs_sys_seg_suballoc(
            &filename_desc,
            &filename_nursury,
            strlen(relname) + 1 + strlen(dir->d.name) + 1,
            1
        );
        if (*relname)
            sprint(newrelname, "%s.%s", filename, dir->d.name);
        else
            strcpy(newrelname, dir->d.name);

        if (dir->d.mode & DMDIR) {
            gsaddir_recursive(newfilename, newrelname, symtable, ppend);
        } else {
            ext = strrchr(dir->d.name, '.');
            if (0) gswarning("%s:%d: Considering %s (%s) of type %s", __FILE__, __LINE__, newfilename, dir->d.name, ext);
            if (ext && !strcmp(ext, ".ags")) {
                int is_doc;

                file_symtable = gscreatesymtable(symtable);
                is_doc = 0;
                file = gsreadfile(newfilename, newrelname, 1, &is_doc, file_symtable);
                if (!file) {
                    if (!is_doc)
                        gswarning("%s:%d: Skipping %s: %r", __FILE__, __LINE__, newfilename)
                    ;
                    continue;
                } else {
                    gsappend_symtable(symtable, file_symtable);
                    **ppend = gs_sys_seg_suballoc(&file_link_desc, &file_link_nursury, sizeof(***ppend), sizeof(void*));
                    (**ppend)->file = file;
                    (**ppend)->next = 0;
                    *ppend = &(**ppend)->next;
                } 
                continue;
            }
            if (ext && (
                !strcmp(ext, ".gs-scheme")
                || !strcmp(ext, ".cgs-scheme")
            ))
                continue;
            gswarning("%s/%s: skipping; not a Global Script file", filename, dir->d.name);
        }
    }
}

gsfiletype
gsaddfile(char *filename, gsvalue *pentry, struct gstype **ptype)
{
    gsparsedfile *parsedfile;
    struct gsfile_symtable *symtable;

    symtable = gscreatesymtable(gscurrent_symtable);

    parsedfile = gsreadfile(filename, "", 0, 0, symtable);

    gsloadfile(parsedfile, symtable, pentry, ptype);

    return parsedfile->type;
}

static long gsgrabline(char *filename, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_data_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_type_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_coercion_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);

#define LINE_LENGTH 0x100
#define NUM_FIELDS 0x20

gsparsedfile *
gsreadfile(char *filename, char *relname, int skip_docs, int *is_doc, struct gsfile_symtable *symtable)
{
    struct uxio_ichannel *chan;
    char line[LINE_LENGTH];
    char *fields[NUM_FIELDS];
    gsparsedfile *parsedfile;
    int pid;
    long n;
    gsfiletype type;
    int lineno;
    enum {
        gsnosection,
        gsdatasection,
        gscodesection,
        gstypesection,
        gscoercionsection,
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
        if (skip_docs) {
            if (gsclosefile(chan, pid) < 0)
                gsfatal("%s: Error in closing file: %r", filename);
            *is_doc = 1;
            return 0;
        }
        type = gsfiledocument;
    } else if (!strcmp(fields[1], ".prefix")) {
        type = gsfileprefix;
    } else {
        gsfatal("%s:%d: Cannot understand directive '%s'", filename, lineno, fields[1]);
        return 0;
    }
    if ((parsedfile = gsparsed_file_alloc(filename, relname, type)) < 0) {
        gsfatal("%s:%d: Cannot allocate input file: %r", filename, lineno);
    }

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
            if (parsedfile->data)
                gsfatal("%s:%d: We already did this section", filename, lineno);
            parsedfile->data = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->data));
            parsedfile->data->first_seg = parsedfile->last_seg;
            parsedfile->data->numitems = 0;
        } else if (!strcmp(fields[1], ".code")) {
            section = gscodesection;
            if (parsedfile->code)
                gsfatal("%s:%d: We already did this section", filename, lineno);
            parsedfile->code = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->code));
            parsedfile->code->numitems = 0;
        } else if (!strcmp(fields[1], ".type")) {
            section = gstypesection;
            if (parsedfile->types)
                gsfatal("%s:%d: We already did this section", filename, lineno);
            parsedfile->types = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->types));
            parsedfile->types->first_seg = parsedfile->last_seg;
            parsedfile->types->numitems = 0;
        } else if (!strcmp(fields[1], ".coercion")) {
            section = gscoercionsection;
            if (parsedfile->coercions)
                gsfatal("%s:%d: We already did this section", filename, lineno);
            parsedfile->coercions = gsparsed_file_extend(parsedfile, sizeof(*parsedfile->coercions));
            parsedfile->coercions->first_seg = parsedfile->last_seg;
            parsedfile->coercions->numitems = 0;
        } else switch (section) {
            case gsnosection:
                gsfatal("%s:%d: Missing section directive", filename, lineno);
                break;
            case gsdatasection:
                if (gsparse_data_item(filename, parsedfile, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading data item: %r", filename, lineno);
                break;
            case gscodesection:
                if (gsparse_code_item(filename, parsedfile, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading code item: %r", filename, lineno);
                break;
            case gstypesection:
                if (gsparse_type_item(filename, parsedfile, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading type item: %r", filename, lineno);
                break;
            case gscoercionsection:
                if (gsparse_coercion_item(filename, parsedfile, chan, line, &lineno, fields, n, symtable) < 0)
                    gsfatal("%s:%d: Error in reading coercion item: %r", filename, lineno);
                break;
            default:
                gsfatal("%s:%d: Parse items in section %d next", __FILE__, __LINE__, section);
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

static
long
gsparse_data_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;
    int i;

    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, numfields);
    parsedfile->data->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymdatalable, fields[0]);
    else
        parsedline->label = 0;

    gssymtable_add_data_item(symtable, parsedline->label, parsedfile, parsedfile->last_seg, parsedline);

    parsedline->directive = gsintern_string(gssymdatadirective, fields[1]);

    if (gssymeq(parsedline->directive, gssymdatadirective, ".closure")) {
        if (numfields < 2+0+1)
            gsfatal("%s:%d: Missing code label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymcodelable, fields[2+0]);
        if (numfields >= 2+1+1)
            parsedline->arguments[1] = gsintern_string(gssymtypelable, fields[2+1]);
        if (numfields > 2+1+1)
            gsfatal("%s:%d: Data item %s has too many arguments; I don't know what they all are", filename, *plineno, fields[0]);
    } else if (gssymeq(parsedline->directive, gssymdatadirective, ".tyapp")) {
        if (numfields < 2+0+1)
            gsfatal("%s:%d: Missing polymorphic data label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymdatalable, fields[2+0]);
        for (i = 1; numfields > 2+i; i++)
            parsedline->arguments[i] = gsintern_string(gssymtypelable, fields[2+i]);
    } else if (gssymeq(parsedline->directive, gssymdatadirective, ".undefined")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing type label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2+0]);
        if (numfields > 2+1)
            gsfatal("%s:%d: Undefined data item %s has too many arguments; I don't know what they all are", filename, *plineno, fields[0]);
    } else if (gssymeq(parsedline->directive, gssymdatadirective, ".cast")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing coercion to apply");
        parsedline->arguments[0] = gsintern_string(gssymcoercionlable, fields[2+0]);
        if (numfields < 2+2)
            gsfatal("%s:%d: Missing target of coercion");
        parsedline->arguments[1] = gsintern_string(gssymdatalable, fields[2+1]);
        if (numfields > 2+2)
            gsfatal("%s:%d: Too many arguments to .cast; I know about the coercion to apply and the data item to cast");
    } else {
        gsfatal("%s:%d: Unimplemented data directive %s", filename, *plineno, fields[1]);
    }

    return 1;
}

static long gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_api_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);

static
long
gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;

    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, numfields);
    parsedfile->code->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymcodelable, fields[0]);
    else
        gsfatal("%s:%d: Missing code label", filename, *plineno);

    gssymtable_add_code_item(symtable, parsedline->label, parsedfile, parsedfile->last_seg, parsedline);

    parsedline->directive = gsintern_string(gssymcodedirective, fields[1]);

    if (gssymeq(parsedline->directive, gssymcodedirective, ".expr")) {
        return gsparse_code_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymeq(parsedline->directive, gssymcodedirective, ".eprog")) {
        return gsparse_api_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: code directive %s", filename, *plineno, fields[1]);
    }

    gsfatal("%s:%d: gsparse_code_item next", __FILE__, __LINE__);

    return -1;
}

static
long
gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        if (gssymeq(parsedline->directive, gssymcodeop, ".tygvar")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .tygvar op", filename, *plineno);
            if (n > 2)
                gsfatal("%s:%s: Too many arguments to .tygvar op", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".gvar")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymdatalable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .gvar op", filename, *plineno);
            if (n > 2)
                gsfatal("%s:%d: Too many arguments to .gvar op", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".arg")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymdatalable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .arg op", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing type on .arg", filename, *plineno);
            for (i = 2; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".app")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuation ops");
            else
                parsedline->label = 0;
            if (n < 3)
                gswarning("%s:%d: Nullary applications don't do anything", filename, *plineno);
            for (i = 2; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".enter")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops");
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing argument to .enter", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymdatalable, fields[2]);
            if (n >= 4)
                gsfatal("%s:%d: Un-recognized arguments to .enter; I only know the code label to enter", filename, *plineno);
            return 0;
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".undef")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops");
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing type on .undef", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
            for (i = 3; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
            return 0;
        } else {
            gsfatal("%s:%d: %s:%d: Unimplemented code op %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading code line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading expression", filename, codedirective->pos.lineno);

    return -1;
}

static
long
gsparse_api_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        if (gssymeq(parsedline->directive, gssymcodeop, ".subcode")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymcodelable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .subcode", filename, *plineno);
            if (n > 2)
                gsfatal("%s:%d: Too many arguments to .subcode", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".body")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing code label", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymcodelable, fields[2]);
            for (i = 3; i < n && strcmp(fields[i], "|"); i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
            ;
            if (i < n)
                parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i])
            ;
            for (i++; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
            ;
            return 0;
        } else {
            gsfatal("%s:%d: %s:%d: Unimplemented api op %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading API line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading API sub-program literal", filename, codedirective->pos.lineno);

    return -1;
}


static long gsparse_type_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *typedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_coerce_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *typedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);

static
long
gsparse_type_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;

    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, numfields);
    parsedfile->types->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymtypelable, fields[0]);
    else
        gsfatal("%s:%d: Missing type label", filename, *plineno);

    gssymtable_add_type_item(symtable, parsedline->label, parsedfile, parsedfile->last_seg, parsedline);

    parsedline->directive = gsintern_string(gssymtypedirective, fields[1]);

    if (gssymeq(parsedline->directive, gssymtypedirective, ".tyexpr")) {
        if (numfields > 2 + 0)
            gsfatal("%s:%d: Too many arguments to .tyexpr", filename, *plineno);
        return gsparse_type_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymeq(parsedline->directive, gssymtypedirective, ".tydefinedprim")) {
        if (numfields < 2 + 1)
            gsfatal("%s:%d: Missing primitive group name", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymprimsetlable, fields[2 + 0]);
        if (numfields < 2 + 2)
            gsfatal("%s:%d: Missing primitive type relative name", filename, *plineno);
        parsedline->arguments[1] = gsintern_string(gssymtypelable, fields[2 + 1]);
        if (numfields < 2 + 3)
            gsfatal("%s:%d: Missing kind on primitive type", filename, *plineno);
        parsedline->arguments[2] = gsintern_string(gssymkindexpr, fields[2 + 2]);
        return 0;
    } else if (gssymeq(parsedline->directive, gssymtypedirective, ".tyapiprim")) {
        if (numfields < 2 + 1)
            gsfatal("%s:%d: Mising primitive group name", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymprimsetlable, fields[2 + 0]);
        if (numfields < 2 + 2)
            gsfatal("%s:%d: Missing primitive type relative name", filename, *plineno);
        parsedline->arguments[1] = gsintern_string(gssymtypelable, fields[2 + 1]);
        if (numfields < 2 + 3)
            gsfatal("%s:%d: Missing kind on primitive type", filename, *plineno);
        parsedline->arguments[2] = gsintern_string(gssymkindexpr, fields[2 + 2]);
        return 0;
    } else if (gssymeq(parsedline->directive, gssymtypedirective, ".tyabstract")) {
        if (numfields < 2 + 1)
            gsfatal("%s:%d: Missing kind on .tyabstract", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymkindexpr, fields[2 + 0]);
        if (numfields > 2 + 1)
            gsfatal("%s:%d: Too many arguments to .tyabstract", filename, *plineno);
        return gsparse_type_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else {
        gsfatal("%s:%d: %s:%d: Unimplemented type directive %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
    }

    gsfatal("%s:%d: gsparse_type_item next", __FILE__, __LINE__);

    return -1;
}

static int gsparse_type_or_coercion_op(char *, struct gsparsedline *, int *, char **, long, gssymboltype);

static
long
gsparse_type_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *typedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymtypeop, fields[1]);

        if (gsparse_type_or_coercion_op(filename, parsedline, plineno, fields, n, gssymtypeop)) {
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".typeapp")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuation ops");
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing argument to .typeapp; types use binary application even in stringcode", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
            if (n > 3)
                gsfatal("%s:%d: Too many arguments to .typeapp; types use binary application even in stringcode", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".tyforall")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0]);
            else
                gsfatal("%s:%d: Labels required on .tyforall", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing kind on .tyforall-bound type variable", filename, *plineno);
            parsedline->arguments[0] = gsintern_string(gssymkindexpr, fields[2]);
            if (n > 3)
                gsfatal("%s:%d: Too many arguments to .tyforall", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".tylet")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0]);
            else
                gsfatal("%s:%d: Labels required on .tylet", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing type label on .tylet", filename, *plineno);
            parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2]);
            if (n < 4)
                gswarning("%s:%d: Consider using .tygvar instead", filename, *plineno);
            for (i = 0; 3 + i < n; i++) {
                parsedline->arguments[1 + i] = gsintern_string(gssymtypelable, fields[3 + i]);
            }
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".tylift")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n > 2)
                gsfatal("%s:%d: Too many arguments to .tylift", filename, *plineno);
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".tyref")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing referent argument to .tyref", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
            for (i = 3; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
            return 0;
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".tysum")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n % 2)
                gsfatal("%s:%d: Can't have odd number of arguments to .tysum", filename, *plineno);
            for (i = 0; 2 + i < n; i += 2) {
                parsedline->arguments[i] = gsintern_string(gssymconstrlable, fields[2 + i]);
                parsedline->arguments[i + 1] = gsintern_string(gssymtypelable, fields[2 + i + 1]);
            }
            return 0;
        } else if (gssymeq(parsedline->directive, gssymtypeop, ".typroduct")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n % 2)
                gsfatal("%s:%d: Can't have odd number of arguments to .typroduct", filename, *plineno);
            for (i = 0; 2 + i < n; i += 2) {
                parsedline->arguments[i] = gsintern_string(gssymfieldlable, fields[2 + i]);
                parsedline->arguments[i + 1] = gsintern_string(gssymtypelable, fields[2 + i + 1]);
            }
            return 0;
        } else {
            gsfatal("%s:%d: %s:%d: Unimplemented type op %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading type line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading type expression", filename, typedirective->pos.lineno);

    return -1;
}

static
long
gsparse_coercion_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    struct gsparsedline *parsedline;

    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, numfields);
    parsedfile->coercions->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymcoercionlable, fields[0]);
    else
        gsfatal("%s:%d: Missing type label", filename, *plineno);

    gssymtable_add_coercion_item(symtable, parsedline->label, parsedfile, parsedfile->last_seg, parsedline);

    parsedline->directive = gsintern_string(gssymcoerciondirective, fields[1]);

    if (gssymeq(parsedline->directive, gssymcoerciondirective, ".tycoercion")) {
        if (numfields > 2 + 0)
            gsfatal("%s:%d: Too many arguments to .tycoercion", filename, *plineno);
        return gsparse_coerce_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else {
        gsfatal("%s:%d: %s:%d: Unimplemented coercion directive %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
    }

    gsfatal("%s:%d: gsparse_coercion_item next", __FILE__, __LINE__);

    return -1;
}

static
long
gsparse_coerce_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *typedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcoercionop, fields[1]);

        if (gsparse_type_or_coercion_op(filename, parsedline, plineno, fields, n, gssymcoercionop)) {
        } else if (gssymeq(parsedline->directive, gssymcoercionop, ".tyinvert")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuations");
            else
                parsedline->label = 0;
            if (n > 2)
                gsfatal("%s:%d: Too many arguments to .tyinvert");
        } else if (gssymeq(parsedline->directive, gssymcoercionop, ".tydefinition")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal op");
            else
                parsedline->label = 0;
            if (n < 2 + 1)
                gsfatal("%s:%d: Missing abstract type to cast to");
            parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2 + 0]);
            for (i = 0; 2 + i < n; i++)
                parsedline->arguments[i] = gsintern_string(gssymtypelable, fields[2 + i]);
            return 0;
        } else {
            gsfatal("%s:%d: %s:%d: Unimplemented coercion op %s", __FILE__, __LINE__, filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading type line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading type expression", filename, typedirective->pos.lineno);

    return -1;
}

static
int
gsparse_type_or_coercion_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n, gssymboltype op)
{
    if (gssymeq(parsedline->directive, op, ".tygvar")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        else
            gsfatal("%s:%d: Labels required on .tygvar", filename, *plineno);
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .tygvar", filename, *plineno);
        return 1;
    } else if (gssymeq(parsedline->directive, op, ".tylambda")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        else
            gsfatal("%s:%d: Labels required on .tylambda", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing kind on .tylambda", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymkindexpr, fields[2]);
        if (n > 3)
            gsfatal("%s:%d: Too many arguments to .tylambda; I only know what the kind is", filename, *plineno);
        return 1;
    } else {
        return 0;
    }
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

enum gsfile_symtable_class {
    gsfile_data_values,
    gsfile_abstype_defns,
    gsfile_code_values,
    gsfile_code_types,
    gsfile_coercion_types,
    gsfile_num_classes,
};

char *gsfile_symtable_class_names[gsfile_num_classes] = {
    "Data (Heap) Values",
    "Byte Code Objects",
};

struct gsfile_symtable {
    struct gsfile_symtable *parent;
    struct gsfile_symtable_item *dataitems, *codeitems, *typeitems, *coercionitems;
    struct gsfile_symtable_data_type_item *datatypes;
    struct gsfile_symtable_type_kind_item *typekinds;
    struct gsfile_symtable_scc_item *sccs;
    struct gsfile_symtable_type_item *types;
    struct gsfile_symtable_kind_item *kinds;
    struct gsfile_symtable_entry *entries[gsfile_num_classes];
};

/* NB: linear-time! */

struct gsfile_symtable_item {
    gsinterned_string key;
    gsparsedfile *file;
    struct gsparsedfile_segment *pseg;
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
    newsymtable->coercionitems = 0;
    newsymtable->datatypes = 0;
    newsymtable->typekinds = 0;
    newsymtable->sccs = 0;
    newsymtable->types = 0;
    newsymtable->kinds = 0;

    return newsymtable;
}

static void gsappend_items(struct gsfile_symtable_item **, struct gsfile_symtable_item *, char*);

void
gsappend_symtable(struct gsfile_symtable *symtable0, struct gsfile_symtable *symtable1)
{
    gsappend_items(&symtable0->codeitems, symtable1->codeitems, "code");
    gsappend_items(&symtable0->dataitems, symtable1->dataitems, "data");
    gsappend_items(&symtable0->typeitems, symtable1->typeitems, "type");
}

static
void
gsappend_items(struct gsfile_symtable_item **dest0, struct gsfile_symtable_item *src0, char *type)
{
    struct gsfile_symtable_item *src, **dest;

    for (src = src0; src; src = src->next) {
        for (dest = dest0; *dest; dest = &(*dest)->next) {
            if (src->key == (*dest)->key) {
                gsfatal("%s:%d: Duplicate %s item %s (duplicate of %s:%d)",
                    src->value->pos.file->name,
                    src->value->pos.lineno,
                    type,
                    src->key->name,
                    (*dest)->value->pos.file->name,
                    (*dest)->value->pos.lineno
                );
            }
        }
        *dest = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**dest), sizeof(gsinterned_string));
        (*dest)->key = src->key;
        (*dest)->file = src->file;
        (*dest)->pseg = src->pseg;
        (*dest)->value = src->value;
        (*dest)->next = 0;
    }
}

void
gssymtable_add_code_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *pcode)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->codeitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate code item %s (duplicate of %s:%d)",
                pcode->pos.file->name,
                pcode->pos.lineno,
                label->name,
                (*p)->value->pos.file->name,
                (*p)->value->pos.lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->pseg = pseg;
    (*p)->value = pcode;
    (*p)->next = 0;
}

void
gssymtable_add_data_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *pdata)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->dataitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate data item %s (duplicate of %s:%d)",
                pdata->pos.file->name,
                pdata->pos.lineno,
                label->name,
                (*p)->value->pos.file->name,
                (*p)->value->pos.lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->pseg = pseg;
    (*p)->value = pdata;
    (*p)->next = 0;
}

void
gssymtable_add_type_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *ptype)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->typeitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate type item %s (duplicate of %s:%d)",
                ptype->pos.file->name,
                ptype->pos.lineno,
                label->name,
                (*p)->value->pos.file->name,
                (*p)->value->pos.lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->pseg = pseg;
    (*p)->value = ptype;
    (*p)->next = 0;
}

void
gssymtable_add_coercion_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *ptype)
{
    struct gsfile_symtable_item **p;

    for (p = &symtable->coercionitems; *p; p = &(*p)->next)
        if ((*p)->key == label)
            gsfatal(
                "%s:%d: Duplicate type item %s (duplicate of %s:%d)",
                ptype->pos.file->name,
                ptype->pos.lineno,
                label->name,
                (*p)->value->pos.file->name,
                (*p)->value->pos.lineno
            )
    ;
    *p = gs_sys_seg_suballoc(&gssymtable_item_segment, &symtable_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->file = file;
    (*p)->pseg = pseg;
    (*p)->value = ptype;
    (*p)->next = 0;
}

void
gssymtable_set_expr_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gsbc_code_item_type *ptype)
{
    gsfatal("%s:%d: %s: gssymtable_set_expr_type next", __FILE__, __LINE__, label->name);
}

struct gsfile_symtable_type_item {
    gsinterned_string key;
    struct gstype *value;
    struct gsfile_symtable_type_item *next;
};

static struct gs_block_class gssymtable_type_item_segment = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol table type entries",
};

static void *symtable_type_item_nursury;

void
gssymtable_set_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gstype *type)
{
    struct gsfile_symtable_type_item **p;

    for (p = &symtable->types; *p; p = &(*p)->next) {
        if ((*p)->key == label)
            gsfatal("%s: Duplicate type", label->name);
    }

    *p = gs_sys_seg_suballoc(&gssymtable_type_item_segment, &symtable_type_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->value = type;
    (*p)->next = 0;
}

static void *gssymtable_get(struct gsfile_symtable *, enum gsfile_symtable_class, gsinterned_string);
static void gssymtable_set(struct gsfile_symtable *, enum gsfile_symtable_class, gsinterned_string, void *);

struct gstype *
gssymtable_get_abstype(struct gsfile_symtable *symtable, gsinterned_string label)
{
    return gssymtable_get(symtable, gsfile_abstype_defns, label);
}

void
gssymtable_set_abstype(struct gsfile_symtable *symtable, gsinterned_string label, struct gstype *defn)
{
    gssymtable_set(symtable, gsfile_abstype_defns, label, defn);
}

struct gsfile_symtable_type_kind_item {
    gsinterned_string key;
    struct gskind *value;
    struct gsfile_symtable_type_kind_item *next;
};

static struct gs_block_class symtable_type_kind_item_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol table kind of type items",
};
void *symtable_type_kind_item_nursury;

void
gssymtable_set_type_expr_kind(struct gsfile_symtable *symtable, gsinterned_string label, struct gskind *kind)
{
    struct gsfile_symtable_type_kind_item **p;

    for (p = &symtable->typekinds; *p; p = &(*p)->next) {
        if ((*p)->key == label)
            gsfatal("%s: Already set kind of type", label->name);
    }
    *p = gs_sys_seg_suballoc(&symtable_type_kind_item_descr, &symtable_type_kind_item_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->value = kind;
    (*p)->next = 0;
}

gsvalue
gssymtable_get_data(struct gsfile_symtable *symtable, gsinterned_string label)
{
    return (gsvalue)gssymtable_get(symtable, gsfile_data_values, label);
}

void
gssymtable_set_data(struct gsfile_symtable *symtable, gsinterned_string label, gsvalue v)
{
    gssymtable_set(symtable, gsfile_data_values, label, (void*)v);
}

struct gsbc_code_item_type *
gssymtable_get_code_type(struct gsfile_symtable *symtable, gsinterned_string label)
{
    return gssymtable_get(symtable, gsfile_code_types, label);
}

void
gssymtable_set_code_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gsbc_code_item_type *v)
{
    return gssymtable_set(symtable, gsfile_code_types, label, v);
}

struct gsbc_coercion_type *
gssymtable_get_coercion_type(struct gsfile_symtable *symtable, gsinterned_string label)
{
    return gssymtable_get(symtable, gsfile_coercion_types, label);
}

void
gssymtable_set_coercion_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gsbc_coercion_type *v)
{
    return gssymtable_set(symtable, gsfile_coercion_types, label, v);
}

struct gsfile_symtable_entry {
    gsinterned_string key;
    void *value;
    struct gsfile_symtable_entry *next;
};

static struct gs_block_class symtable_entry_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol table entries",
};
static void *symtable_entry_nursury;

static
void *
gssymtable_get(struct gsfile_symtable *symtable, enum gsfile_symtable_class class, gsinterned_string label)
{
    struct gsfile_symtable_entry *p;

    for (; symtable; symtable = symtable->parent) {
        for (p = symtable->entries[class]; p; p = p->next) {
            if (p->key == label)
                return p->value
            ;
        }
    }

    return 0;
}

static
void
gssymtable_set(struct gsfile_symtable *symtable, enum gsfile_symtable_class class, gsinterned_string label, void *v)
{
    struct gsfile_symtable_entry **p;

    for (p = &symtable->entries[class]; *p; p = &(*p)->next) {
        if ((*p)->key == label)
            gsfatal("Duplicate %s %s", gsfile_symtable_class_names[class], label->name);
    }

    *p = gs_sys_seg_suballoc(&symtable_entry_descr, &symtable_entry_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->value = v;
    (*p)->next = 0;
}

struct gsbco *
gssymtable_get_code(struct gsfile_symtable *symtable, gsinterned_string label)
{
    return gssymtable_get(symtable, gsfile_code_values, label);
}

void
gssymtable_set_code(struct gsfile_symtable *symtable, gsinterned_string label, struct gsbco *v)
{
    gssymtable_set(symtable, gsfile_code_values, label, v);
}

struct gsfile_symtable_data_type_item {
    gsinterned_string key;
    struct gstype *value;
    struct gsfile_symtable_data_type_item *next;
};

struct gstype *
gssymtable_get_data_type(struct gsfile_symtable *symtable, gsinterned_string label)
{
    struct gsfile_symtable_data_type_item *p;

    for (p = symtable->datatypes; p; p = p->next) {
        if (p->key == label)
            return p->value
        ;
    }

    return 0;
}

void
gssymtable_set_data_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gstype *v)
{
    struct gsfile_symtable_data_type_item **p;

    for (p = &symtable->datatypes; *p; p = &(*p)->next) {
        if ((*p)->key == label)
            gsfatal("%s: Already set type of data item", label->name)
        ;
    }

    *p = gs_sys_seg_suballoc(&symtable_entry_descr, &symtable_entry_nursury, sizeof(**p), sizeof(gsinterned_string));
    (*p)->key = label;
    (*p)->value = v;
    (*p)->next = 0;
}

struct gskind *
gssymtable_get_type_expr_kind(struct gsfile_symtable *symtable, gsinterned_string label)
{
    struct gsfile_symtable_type_kind_item *p;

    for (p = symtable->typekinds; p; p = p->next) {
        if (p->key == label)
            return p->value;
    }

    return 0;
}

struct gstype *
gssymtable_get_type(struct gsfile_symtable *symtable, gsinterned_string label)
{
    struct gsfile_symtable_type_item *p;

    for (p = symtable->types; p; p = p->next) {
        if (p->key == label)
            return p->value;
    }

    return 0;
}

struct gsfile_symtable_kind_item {
    gsinterned_string key;
    struct gsbc_kind *value;
    struct gsfile_symtable_kind_item *next;
};

struct gsbc_kind *
gssymtable_get_kind(struct gsfile_symtable *symtable, gsinterned_string label)
{
    struct gsfile_symtable_kind_item *p;

    for (p = symtable->kinds; p; p = p->next) {
        if (p->key == label)
            return p->value;
    }

    return 0;
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
                        res.pseg = p->pseg;
                        res.v = p->value;
                        return res;
                    }
                }
                break;
            case gssymcodelable:
                for (p = symtable->codeitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymcodelable;
                        res.pseg = p->pseg;
                        res.v = p->value;
                        return res;
                    }
                }
                break;
            case gssymtypelable:
                for (p = symtable->typeitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymtypelable;
                        res.pseg = p->pseg;
                        res.v = p->value;
                        return res;
                    }
                }
                break;
            case gssymcoercionlable:
                for (p = symtable->coercionitems; p; p = p->next) {
                    if (p->key == label) {
                        res.file = p->file;
                        res.type = gssymcoercionlable;
                        res.pseg = p->pseg;
                        res.v = p->value;
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
        case gssymcoercionlable:
            strtype = "coercion label";
            break;
        default:
            gsfatal("%s:%d: Cannot translate symbol type %d to a string", __FILE__, __LINE__, label->type);
    }

    gsfatal("%s:%d: Unknown %s '%s'", filename, lineno, strtype, label->name);

    return res;
}

struct gsfile_symtable_scc_item {
    struct gsbc_item key;
    struct gsbc_scc *value;
    struct gsfile_symtable_scc_item *next;
};

static struct gs_block_class gsfile_symtable_scc_item_descr = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Symbol table track SCC items belong to",
};
static void *gsfile_symtable_scc_item_nursury;

struct gsbc_scc *
gssymtable_get_scc(struct gsfile_symtable *symtable, struct gsbc_item item)
{
    struct gsfile_symtable_scc_item *pscc_item;

    for (pscc_item = symtable->sccs; pscc_item; pscc_item = pscc_item->next) {
        if (gsbc_item_eq(pscc_item->key, item))
            return pscc_item->value;
    }
    return 0;
}

void
gssymtable_set_scc(struct gsfile_symtable *symtable, struct gsbc_item item, struct gsbc_scc *pscc)
{
    struct gsfile_symtable_scc_item **ppscc_item;

    for (ppscc_item = &symtable->sccs; *ppscc_item; ppscc_item = &(*ppscc_item)->next) {
        if (gsbc_item_eq((*ppscc_item)->key, item))
            gsfatal("%s:%d: Item already has SCC", item.v->pos.file->name, item.v->pos.lineno);
    }

    *ppscc_item = gs_sys_seg_suballoc(&gsfile_symtable_scc_item_descr, &gsfile_symtable_scc_item_nursury, sizeof(**ppscc_item), sizeof(*ppscc_item));
    (*ppscc_item)->next = 0;
    (*ppscc_item)->key = item;
    (*ppscc_item)->value = pscc;
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

/* Loader */

static void gsload_scc(gsparsedfile *, struct gsfile_symtable *, struct gsbc_scc *, gsvalue *, struct gstype **);

void
gsloadfile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, gsvalue *pentry, struct gstype **ptype)
{
    struct gsbc_scc *pscc, *p;
    int contains_entry;

    switch (parsedfile->type) {
        case gsfileprefix:
            pscc = gsbc_topsortfile(parsedfile, symtable);
            for (p = pscc; p; p = p->next_scc) {
                gsload_scc(parsedfile, symtable, p, 0, 0);
            }
            return;
        case gsfiledocument:
            pscc = gsbc_topsortfile(parsedfile, symtable);
            for (p = pscc; p; p = p->next_scc) {
                contains_entry = !p->next_scc;
                gsload_scc(parsedfile, symtable, p, contains_entry ? pentry : 0, contains_entry ? ptype : 0);
            }
            return;
        default:
            gsfatal("%s: Unknown file type %d in gsbytecompile", parsedfile->name->name, parsedfile->type);
    }
}

static
void
gsload_scc(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, struct gsbc_scc *pscc, gsvalue *pentry, struct gstype **ptype)
{
    struct gsbc_scc *p;
    struct gsbc_item items[MAX_ITEMS_PER_SCC];
    struct gstype *types[MAX_ITEMS_PER_SCC], *defns[MAX_ITEMS_PER_SCC];
    struct gskind *kinds[MAX_ITEMS_PER_SCC];
    gsvalue heap[MAX_ITEMS_PER_SCC], errors[MAX_ITEMS_PER_SCC], indir[MAX_ITEMS_PER_SCC];
    struct gsbco *bcos[MAX_ITEMS_PER_SCC];
    int n, i;

    n = 0;

    /* §section{} */

    for (p = pscc; p; p = p->next_item) {
        if (n >= MAX_ITEMS_PER_SCC)
            gsfatal("%s:%d: Too many items in this SCC; max 0x%x", p->item.v->pos.file->name, p->item.v->pos.lineno, MAX_ITEMS_PER_SCC)
        ;
        items[n++] = p->item;
    }

    /* §section{Type-checking} */

    gstypes_alloc_for_scc(symtable, items, types, defns, n);
    gstypes_process_type_declarations(symtable, items, kinds, n);
    gstypes_compile_types(symtable, items, types, defns, n);
    gstypes_kind_check_scc(symtable, items, types, kinds, n);
    gstypes_process_type_signatures(symtable, items, ptype, n);
    gstypes_type_check_scc(symtable, items, types, kinds, ptype, n);

    /* §section{Byte-compilation} */

    gsbc_alloc_data_for_scc(symtable, items, heap, errors, indir, n);
    gsbc_alloc_code_for_scc(symtable, items, bcos, n);
    gsbc_bytecompile_scc(symtable, items, heap, errors, bcos, n);

    if (pentry) {
        for (i = 0; i < n; i++) {
            if (
                items[i].type == gssymdatalable
                && items[i].v == GSDATA_SECTION_FIRST_ITEM(parsedfile->data)
            ) {
                if (heap[i])
                    *pentry = heap[i];
                else if (errors[i])
                    *pentry = errors[i];
                else if (indir[i])
                    *pentry = indir[i];
                else
                    gsfatal_unimpl(__FILE__, __LINE__, "%s: Entry point: couldn't find in any SCC");
                if (items[i].v->label) {
                    gsfatal_unimpl_input(__FILE__, __LINE__, items[i].v, "set *ptype");
                } else
                    /* Don't have to save §c{*ptype} in this case, because we handle that while doing the initial type-checking */
                ;
                goto have_entry;
            }
        }
        gsfatal("%s: Couldn't find entry point", parsedfile->name->name);
    have_entry:
        ;
    }
}
