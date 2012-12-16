/* §source.file Loading String Code Files & Other Source Files */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "gsinputfile.h"
#include "gsinputalloc.h"
#include "gsbytecompile.h"
#include "gsregtables.h"
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
    struct gsbio_dir *d;
    d = gsbio_stat(filename);
    if (!d) gsfatal("%r");
    return d->d.mode & DMDIR;
}

#define FILE_NAME_SIZE_LIMIT 0x100

void
gsadd_global_gslib()
{
    struct uxio_ichannel *chan;
    char globalscript[FILE_NAME_SIZE_LIMIT], buf[FILE_NAME_SIZE_LIMIT];
    long n;

    if (!(chan = gsbio_envvar_iopen("GLOBALSCRIPT"))) {
        strncpy(globalscript, "", FILE_NAME_SIZE_LIMIT);
        goto set_buf;
    }
    if ((n = gsbio_get_contents(chan, globalscript, FILE_NAME_SIZE_LIMIT)) < 0)
        gsfatal("Error reading $GLOBALSCRIPT: %r")
    ;
    if (n >= FILE_NAME_SIZE_LIMIT) {
        globalscript[n - 1] = 0;
        gsfatal("%s:%d: $GLOBALSCRIPT too large (only read %s)", __FILE__, __LINE__, globalscript);
    }
    if (gsbio_device_iclose(chan) < 0)
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

static void gsloadfile(gsparsedfile *, struct gsfile_symtable *, struct gspos *, gsvalue *, struct gstype **);

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

    for (pfile = file_list; pfile; pfile = pfile->next)
        gsloadfile(pfile->file, symtable, 0, 0, 0)
    ;

    gscurrent_symtable = symtable;
}

static gsparsedfile *gsreadfile(char *filename, char *relname, int skip_docs, int *, int is_ags, struct gsfile_symtable *symtable);

static struct gs_block_class filename_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Filenames for gsaddir_recursive",
};
static void *filename_nursury;

static struct gs_block_class file_link_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "File list links for returning file list to gsaddir",
};
static void *file_link_nursury;

static
void
gsaddir_recursive(char *filename, char *relname, struct gsfile_symtable *symtable, struct gsfile_link ***ppend)
{
    struct uxio_dir_ichannel *chan;
    struct gsbio_dir *dir;

    if (!(chan = gsbio_dir_iopen(filename, OREAD)))
        gsfatal("open: %r")
    ;

    while (dir = gsbio_read_stat(chan)) {
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
                file = gsreadfile(newfilename, newrelname, 1, &is_doc, 1, file_symtable);
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
            if (!strcmp(dir->d.name, "mkfile")) continue;
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
gsaddfile(char *filename, struct gspos *pentrypos, gsvalue *pentry, struct gstype **ptype)
{
    gsparsedfile *parsedfile;
    struct gsfile_symtable *symtable;
    char *ext;

    ext = strrchr(filename, '.');

    symtable = gscreatesymtable(gscurrent_symtable);

    parsedfile = gsreadfile(filename, "", 0, 0, ext && !strcmp(ext, ".ags"), symtable);

    gsloadfile(parsedfile, symtable, pentrypos, pentry, ptype);

    return parsedfile->type;
}

static long gsgrabline(char *filename, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_data_item(char *filename, int is_ags, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_type_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);
static long gsparse_coercion_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable);

#define LINE_LENGTH 0x400
#define NUM_FIELDS 0x20

gsparsedfile *
gsreadfile(char *filename, char *relname, int skip_docs, int *is_doc, int is_ags, struct gsfile_symtable *symtable)
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
                if (gsparse_data_item(filename, is_ags, parsedfile, chan, line, &lineno, fields, n, symtable) < 0)
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

    if (!gsbio_idevice_at_eof(chan))
        gsfatal("%s:%d: Expected EOF", filename, lineno);

    if (gsclosefile(chan, pid) < 0)
        gsfatal("%s: Error in closing file: %r", filename);

    return parsedfile;
}

static
long
gsparse_data_item(char *filename, int is_ags, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    static gsinterned_string gssymclosure, gssymtyapp, gssymrecord, gssymconstr, gssymrune, gssymstring, gssymlist, gssymundefined, gssymcast;

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

    if (gssymceq(parsedline->directive, gssymclosure, gssymdatadirective, ".closure")) {
        if (numfields < 2+0+1)
            gsfatal("%s:%d: Missing code label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymcodelable, fields[2+0]);
        if (numfields >= 2+1+1)
            parsedline->arguments[1] = gsintern_string(gssymtypelable, fields[2+1]);
        if (numfields > 2+1+1)
            gsfatal("%s:%d: Data item %s has too many arguments; I don't know what they all are", filename, *plineno, fields[0]);
    } else if (gssymceq(parsedline->directive, gssymtyapp, gssymdatadirective, ".tyapp")) {
        if (numfields < 2+0+1)
            gsfatal("%s:%d: Missing polymorphic data label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymdatalable, fields[2+0]);
        for (i = 1; numfields > 2+i; i++)
            parsedline->arguments[i] = gsintern_string(gssymtypelable, fields[2+i]);
    } else if (gssymceq(parsedline->directive, gssymrecord, gssymdatadirective, ".record")) {
        if (numfields % 2)
            gsfatal("%s:%d: Odd number of arguments to .record", filename, *plineno);
        for (i = 0; numfields > 2 + i; i+= 2) {
            parsedline->arguments[i] = gsintern_string(gssymfieldlable, fields[2+i]);
            parsedline->arguments[i+1] = gsintern_string(gssymdatalable, fields[2+i+1]);
        }
    } else if (gssymceq(parsedline->directive, gssymconstr, gssymdatadirective, ".constr")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing sum type argument to .constr", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2+0]);
        if (numfields < 2+2)
            gsfatal("%s:%d: Missing constructor argument to .constr", filename, *plineno);
        parsedline->arguments[1] = gsintern_string(gssymconstrlable, fields[2+1]);
        if (numfields == 2+3) {
            parsedline->arguments[4 - 2] = gsintern_string(gssymdatalable, fields[4]);
        } else {
            if ((numfields - (2+2)) % 2)
                gsfatal("%s:%d: Odd number of arguments to .constr when field/value pairs expected", filename, *plineno);
            for (i = 2; 2 + i < numfields; i += 2) {
                parsedline->arguments[i] = gsintern_string(gssymfieldlable, fields[2+i]);
                parsedline->arguments[i+1] = gsintern_string(gssymdatalable, fields[2+i+1]);
            }
        }
    } else if (gssymceq(parsedline->directive, gssymrune, gssymdatadirective, ".rune")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing rune literal", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymruneconstant, fields[2+0]);
        if (numfields > 2+1)
            gsfatal("%s:%d: Too many arguments to .rune; I know about the rune literal to use");
    } else if (is_ags && gssymceq(parsedline->directive, gssymstring, gssymdatadirective, ".string")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing string literal", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymstringconstant, fields[2+0]);
        if (numfields >= 2+2)
            parsedline->arguments[1] = gsintern_string(gssymdatalable, fields[2+1]);
        if (numfields > 2+2)
            gsfatal("%s:%d: Too many arguments to .string");
    } else if (is_ags && gssymceq(parsedline->directive, gssymlist, gssymdatadirective, ".list")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing element type", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2+0]);
        for (i = 1; 2 + i < numfields && strcmp(fields[2 + i], "|"); i++)
            parsedline->arguments[i] = gsintern_string(gssymdatalable, fields[2+i]);
        if (2 + i < numfields)
            gsfatal(UNIMPL("%s:%d: Dotted .lists next"), filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymundefined, gssymdatadirective, ".undefined")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing type label", filename, *plineno);
        parsedline->arguments[0] = gsintern_string(gssymtypelable, fields[2+0]);
        if (numfields > 2+1)
            gsfatal("%s:%d: Undefined data item %s has too many arguments; I don't know what they all are", filename, *plineno, fields[0]);
    } else if (gssymceq(parsedline->directive, gssymcast, gssymdatadirective, ".cast")) {
        if (numfields < 2+1)
            gsfatal("%s:%d: Missing coercion to apply");
        parsedline->arguments[0] = gsintern_string(gssymcoercionlable, fields[2+0]);
        if (numfields < 2+2)
            gsfatal("%s:%d: Missing target of coercion");
        parsedline->arguments[1] = gsintern_string(gssymdatalable, fields[2+1]);
        if (numfields > 2+2)
            gsfatal("%s:%d: Too many arguments to .cast; I know about the coercion to apply and the data item to cast");
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Unimplemented data directive %s", filename, *plineno, fields[1]);
    }

    return 1;
}

static long gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_force_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_strict_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_ubcase_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);
static long gsparse_api_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields);

static
long
gsparse_code_item(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields, ulong numfields, struct gsfile_symtable *symtable)
{
    static gsinterned_string gssymexpr, gssymforcecont, gssymstrictcont, gssymubcasecont, gssymeprog;

    struct gsparsedline *parsedline;

    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, numfields);
    parsedfile->code->numitems++;

    if (*fields[0])
        parsedline->label = gsintern_string(gssymcodelable, fields[0]);
    else
        gsfatal("%s:%d: Missing code label", filename, *plineno);

    gssymtable_add_code_item(symtable, parsedline->label, parsedfile, parsedfile->last_seg, parsedline);

    parsedline->directive = gsintern_string(gssymcodedirective, fields[1]);

    if (gssymceq(parsedline->directive, gssymexpr, gssymcodedirective, ".expr")) {
        if (numfields > 2)
            gsfatal("%s:%d: Too many arguments to .expr", filename, *plineno)
        ;
        return gsparse_code_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymceq(parsedline->directive, gssymforcecont, gssymcodedirective, ".forcecont")) {
        if (numfields > 2)
            gsfatal("%s:%d: Too many arguments to .forcecont", filename, *plineno)
        ;
        return gsparse_force_cont_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymceq(parsedline->directive, gssymstrictcont, gssymcodedirective, ".strictcont")) {
        if (numfields > 2)
            gsfatal("%s:%d: Too many arguments to .strictcont", filename, *plineno)
        ;
        return gsparse_strict_cont_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymceq(parsedline->directive, gssymubcasecont, gssymcodedirective, ".ubcasecont")) {
        if (numfields > 2)
            gsfatal("%s:%d: Too many arguments to .ubcasecont", filename, *plineno)
        ;
        return gsparse_ubcase_cont_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else if (gssymceq(parsedline->directive, gssymeprog, gssymcodedirective, ".eprog")) {
        if (numfields < 3)
            gsfatal("%s:%d: Missing primset argument to .eprog", filename, *plineno)
        ;
        parsedline->arguments[2 - 2] = gsintern_string(gssymprimsetlable, fields[2]);
        if (numfields < 4)
            gsfatal("%s:%d: Missing prim argument to .eprog", filename, *plineno)
        ;
        parsedline->arguments[3 - 2] = gsintern_string(gssymtypelable, fields[3]);
        if (numfields > 4)
            gsfatal("%s:%d: Too many arguments to .eprog", filename, *plineno)
        ;
        return gsparse_api_ops(filename, parsedfile, parsedline, chan, line, plineno, fields);
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: code directive %s", filename, *plineno, fields[1]);
    }

    gsfatal("%s:%d: gsparse_code_item next", __FILE__, __LINE__);

    return -1;
}

static int gsparse_code_type_fv_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_code_type_let_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_value_arg_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_value_fv_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_thunk_alloc_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_value_alloc_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_cont_push_op(char *, struct gsparsedline *, int *, char **, long);
static int gsparse_code_terminal_expr_op(char *, gsparsedfile *, struct uxio_ichannel *, char *, struct gsparsedline *, int *, char **, long);

static
long
gsparse_code_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    static gsinterned_string gssymtyarg, gssymarg, gssymeprim;

    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        if (gsparse_code_type_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gssymceq(parsedline->directive, gssymtyarg, gssymcodeop, ".tyarg")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .tyarg op", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing kind on .tyarg", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymkindexpr, fields[2]);
            if (n > 4)
                gsfatal("%s:%d: Too many arguments to .tyarg", filename, *plineno);
        } else if (gsparse_value_arg_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gssymceq(parsedline->directive, gssymarg, gssymcodeop, ".arg")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymdatalable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on .arg op", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing type on .arg", filename, *plineno);
            for (i = 2; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        } else if (gsparse_thunk_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gssymceq(parsedline->directive, gssymeprim, gssymcodeop, ".eprim")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymdatalable, fields[0]);
            else
                gsfatal("%s:%d: Missing label on allocation op", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing primset on .eprim", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymprimsetlable, fields[2]);
            if (n < 4)
                gsfatal("%s:%d: Missing prim type name on .eprim", filename, *plineno);
            parsedline->arguments[3 - 2] = gsintern_string(gssymtypelable, fields[3]);
            if (n < 5)
                gsfatal("%s:%d: Missing prim name on .eprim", filename, *plineno);
            parsedline->arguments[4 - 2] = gsintern_string(gssymdatalable, fields[4]);
            if (n < 6)
                gsfatal("%s:%d: Missing declared type on .eprim", filename, *plineno);
            parsedline->arguments[5 - 2] = gsintern_string(gssymtypelable, fields[5]);
            for (i = 6; i < n && strcmp(fields[i], "|"); i++) {
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
            }
            if (i < n) {
                parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
                i++;
            }
            for (; i < n; i++) {
                parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
            }
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return 0;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Unimplemented code op %s", filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading code line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading expression", filename, codedirective->pos.lineno);

    return -1;
}

static int gsparse_cont_arg(char *, struct gsparsedline *, int *, char **, long);

static
long
gsparse_force_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);
        if (gsparse_code_type_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_thunk_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return 0;
        } else {
            gsfatal(UNIMPL("%s:%d: Unimplemented force continuation op %s"), filename, *plineno, fields[1]);
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
gsparse_strict_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);
        if (gsparse_code_type_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return 0;
        } else {
            gsfatal(UNIMPL("%s:%d: Unimplemented force continuation op %s"), filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading code line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading expression", filename, codedirective->pos.lineno);

    return -1;
}

static int gsparse_field_cont_arg(char *, struct gsparsedline *, int *, char **, long);

static
long
gsparse_ubcase_cont_ops(char *filename, gsparsedfile *parsedfile, struct gsparsedline *codedirective, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    struct gsparsedline *parsedline;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);
        if (gsparse_code_type_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_field_cont_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_thunk_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return 0;
        } else {
            gsfatal(UNIMPL("%s:%d: Unimplemented un-boxed case continuation op %s"), filename, *plineno, fields[1]);
        }
    }
    if (n < 0)
        gsfatal("%s:%d: Error in reading code line: %r", filename, *plineno);
    else
        gsfatal("%s:%d: EOF in middle of reading expression", filename, codedirective->pos.lineno);

    return -1;
}

static
int
gsparse_code_type_fv_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymtygvar, gssymtyfv;

    if (gssymceq(parsedline->directive, gssymtygvar, gssymcodeop, ".tygvar")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .tygvar op", filename, *plineno);
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .tygvar op", filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymtyfv, gssymcodeop, ".tyfv")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .tyfv op", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing kind on .tyfv", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymkindexpr, fields[2]);
        if (n > 4)
            gsfatal("%s:%d: Too many arguments to .tyfv", filename, *plineno);
    } else {
        return 0;
    }
    return 1;
}

static
int
gsparse_code_type_let_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymtylet;

    int i;

    if (gssymceq(parsedline->directive, gssymtylet, gssymcodeop, ".tylet")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        else
            gsfatal("%s:%d: Labels required on .tylet", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing type label on .tylet", filename, *plineno);
        if (n < 4)
            gswarning("%s:%d: Consider using .tygvar instead", filename, *plineno);
        for (i = 2; i < n; i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        }
    } else {
        return 0;
    }
    return 1;
}

static
int
gsparse_value_arg_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymlarg;

    int i;

    if (gssymceq(parsedline->directive, gssymlarg, gssymcodeop, ".larg")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .larg op", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing type on .larg", filename, *plineno);
        for (i = 2; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
    } else {
        return 0;
    }
    return 1;
}

#define CHECK_GVAR_LABEL(op) \
    do { \
        if (*fields[0]) \
            parsedline->label = gsintern_string(gssymdatalable, fields[0]) \
        ; else \
            gsfatal("%s:%d: Missing label on " op " op", filename, *plineno) \
        ; \
    } while (0)

static
int
gsparse_value_fv_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymsubcode, gssymcogvar, gssymgvar, gssymrune, gssymnatural, gssymfv, gssymefv;

    int i;

    if (gssymceq(parsedline->directive, gssymsubcode, gssymcodeop, ".subcode")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymcodelable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .subcode", filename, *plineno);
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .subcode", filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymcogvar, gssymcodeop, ".cogvar")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymcoercionlable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .cogvar op", filename, *plineno);
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .cogvar op", filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymgvar, gssymcodeop, ".gvar")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .gvar op", filename, *plineno);
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .gvar op", filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymrune, gssymcodeop, ".rune")) {
        CHECK_GVAR_LABEL(".rune");
        if (n < 2+1)
            gsfatal("%s:%d: Missing rune literal", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymruneconstant, fields[2]);
        if (n > 2+1)
            gsfatal("%s:%d: Too many arguments to .rune; I know about the rune literal to use");
    } else if (gssymceq(parsedline->directive, gssymnatural, gssymcodeop, ".natural")) {
        CHECK_GVAR_LABEL(".rune");
        if (n < 2 + 1)
            gsfatal("%s:%d: Missing natural number literal", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymnaturalconstant, fields[2]);
        if (n > 2 + 1)
            gsfatal("%s:%d: Too many arguments to .natural; I know about the natural literal to use");
    } else if (gssymceq(parsedline->directive, gssymfv, gssymcodeop, ".fv")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .fv op", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing type on .fv", filename, *plineno);
        for (i = 2; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
    } else if (gssymceq(parsedline->directive, gssymefv, gssymcodeop, ".efv")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .efv op", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing type on .efv", filename, *plineno);
        for (i = 2; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
    } else {
        return 0;
    }
    return 1;
}

static
int
gsparse_cont_arg(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymkarg;

    int i;

    if (gssymceq(parsedline->directive, gssymkarg, gssymcodeop, ".karg")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%s:%d: Missing label on .karg op", filename, *plineno);
        if (n < 3)
            gsfatal("%s:%d: Missing type on .karg", filename, *plineno);
        for (i = 2; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
    } else {
        return 0;
    }

    return 1;
}

/* ↓ Only parse ops legal in a .eprog */
static
int
gsparse_thunk_alloc_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymalloc;

    int i;

    if (gssymceq(parsedline->directive, gssymalloc, gssymcodeop, ".alloc")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0])
        ; else {
            gswarning("%s:%d: Missing label on .alloc makes it a no-op", filename, *plineno);
            parsedline->label = 0;
        }
        if (n < 3)
            gsfatal("%s:%d: Missing subexpression on .alloc", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymcodelable, fields[2]);
        for (i = 3; i < n && strcmp(fields[i], "|"); i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        }
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
        }
    } else {
        return 0;
    }
    return 1;
}

#define STORE_VALUE_ALLOC_OP_LABEL(op) \
    do { \
        if (*fields[0]) \
            p->label = gsintern_string(gssymdatalable, fields[0]) \
        ; else { \
            gswarning("%P: Missing label on %s makes it a no-op", op, p->pos); \
            p->label = 0; \
        } \
    } while (0)

static
int
gsparse_value_alloc_op(char *filename, struct gsparsedline *p, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymprim, gssymconstr, gssymexconstr, gssymrecord, gssymfield;

    int i;

    if (gssymceq(p->directive, gssymprim, gssymcodeop, ".prim")) {
        if (*fields[0])
            p->label = gsintern_string(gssymdatalable, fields[0])
        ; else
            gsfatal("%P: Missing label on allocation op", p->pos)
        ;
        if (n < 3)
            gsfatal("%P: Missing primset on .prim", p->pos)
        ;
        p->arguments[2 - 2] = gsintern_string(gssymprimsetlable, fields[2]);
        if (n < 4)
            gsfatal("%P: Missing prim name on .eprim", p->pos)
        ;
        p->arguments[3 - 2] = gsintern_string(gssymdatalable, fields[3]);
        if (n < 5)
            gsfatal("%P: Missing declared type on .eprim", p->pos)
        ;
        p->arguments[4 - 2] = gsintern_string(gssymtypelable, fields[4]);
        for (i = 5; i < n && strcmp(fields[i], "|"); i++)
            p->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
        if (i < n) {
            p->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++)
            p->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
        ;
    } else if (gssymceq(p->directive, gssymconstr, gssymcodeop, ".constr")) {
        STORE_VALUE_ALLOC_OP_LABEL(".constr");
        if (n < 3)
            gsfatal("%P: Missing type on .constr", p->pos)
        ;
        p->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
        if (n < 4)
            gsfatal("%P: Missing constructor on .constr", p->pos)
        ;
        p->arguments[3 - 2] = gsintern_string(gssymconstrlable, fields[3]);
        if (n == 5)
            gsfatal_unimpl(__FILE__, __LINE__, "%P: gsparse_value_alloc_op(.constr with simple argument)", p->pos)
        ; else {
            if (n % 2)
                gsfatal("%P: Odd number of arguments to .constr when expecting field/value pairs", p->pos)
            ;
            for (i = 4; i < n; i += 2) {
                p->arguments[i - 2] = gsintern_string(gssymfieldlable, fields[i]);
                p->arguments[i + 1 - 2] = gsintern_string(gssymdatalable, fields[i + 1]);
            }
        }
    } else if (gssymceq(p->directive, gssymexconstr, gssymcodeop, ".exconstr")) {
        STORE_VALUE_ALLOC_OP_LABEL(".exconstr");
        if (n < 3)
            gsfatal("%P: Missing type on .constr", p->pos)
        ;
        p->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
        if (n < 4)
            gsfatal("%P: Missing constructor on .constr", p->pos)
        ;
        p->arguments[3 - 2] = gsintern_string(gssymconstrlable, fields[3]);
        for (i = 4; i < n && strcmp(fields[i], "|"); i++)
            p->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
        if (i < n) {
            p->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        if (n == i +1)
            gsfatal(UNIMPL("%P: gsparse_value_alloc_op(.exconstr with simple argument)"), p->pos)
        ; else {
            if ((n - i) % 2)
                gsfatal("%P: Odd number of arguments to .exconstr when expecting field/value pairs", p->pos)
            ;
            for (; i < n; i += 2) {
                p->arguments[i - 2] = gsintern_string(gssymfieldlable, fields[i]);
                p->arguments[i + 1 - 2] = gsintern_string(gssymdatalable, fields[i + 1]);
            }
        }
    } else if (gssymceq(p->directive, gssymrecord, gssymcodeop, ".record")) {
        if (*fields[0])
            p->label = gsintern_string(gssymdatalable, fields[0])
        ; else {
            gswarning("%s:%d: Missing label on .record makes it a no-op", filename, *plineno);
            p->label = 0;
        }
        if (n % 2)
            gsfatal("%s:%d: Odd number of arguments to .record", filename, *plineno);
        for (i = 2; i < n; i += 2) {
            p->arguments[i - 2] = gsintern_string(gssymfieldlable, fields[i]);
            p->arguments[i + 1 - 2] = gsintern_string(gssymdatalable, fields[i + 1]);
        }
    } else if (gssymceq(p->directive, gssymfield, gssymcodeop, ".field")) {
        STORE_VALUE_ALLOC_OP_LABEL(".field");
        if (n < 3)
            gsfatal("%P: Missing field on .field", p->pos)
        ;
        p->arguments[2 - 2] = gsintern_string(gssymfieldlable, fields[2]);
        if (n < 4)
            gsfatal("%P: Missing record on .field", p->pos)
        ;
        p->arguments[3 - 2] = gsintern_string(gssymdatalable, fields[3]);
        if (n > 4)
            gsfatal("%P: Too many arguments to .field", p->pos)
        ;
    } else {
        return 0;
    }
    return 1;
}

#define NO_LABEL_ON_CONT() \
    do { \
        if (*fields[0]) \
            gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno) \
        ; else \
            parsedline->label = 0 \
        ; \
    } while (0)

static
int
gsparse_cont_push_op(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymlift, gssymcoerce, gssymapp, gssymforce, gssymstrict, gssymubanalyze;

    int i;

    if (gssymceq(parsedline->directive, gssymlift, gssymcodeop, ".lift")) {
        if (*fields[0])
            gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
        else
            parsedline->label = 0;
        if (n > 2)
            gsfatal("%s:%d: Too many arguments to .lift", filename, *plineno);
    } else if (gssymceq(parsedline->directive, gssymcoerce, gssymcodeop, ".coerce")) {
        if (n < 3)
            gsfatal("%s:%d: Missing coercion to apply");
        parsedline->arguments[0] = gsintern_string(gssymcoercionlable, fields[2+0]);
        for (i = 1; 2+i < n; i++) {
            parsedline->arguments[i] = gsintern_string(gssymtypelable, fields[2+i]);
        }
    } else if (gssymceq(parsedline->directive, gssymapp, gssymcodeop, ".app")) {
        if (*fields[0])
            gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
        else
            parsedline->label = 0;
        if (n < 3)
            gswarning("%s:%d: Nullary applications don't do anything", filename, *plineno);
        for (i = 2; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
    } else if (gssymceq(parsedline->directive, gssymforce, gssymcodeop, ".force")) {
        if (*fields[0])
            gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
        else
            parsedline->label = 0;
        if (n < 3)
            gsfatal("%s:%d: Missing continuation on .force", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymcodelable, fields[2]);
        for (i = 3; i < n && strcmp(fields[i], "|"); i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        }
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
        }
    } else if (gssymceq(parsedline->directive, gssymstrict, gssymcodeop, ".strict")) {
        NO_LABEL_ON_CONT();
        if (n < 3)
            gsfatal("%s:%d: Missing continuation on .force", filename, *plineno)
        ;
        parsedline->arguments[2 - 2] = gsintern_string(gssymcodelable, fields[2]);
        for (i = 3; i < n && strcmp(fields[i], "|"); i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
        ;
    } else if (gssymceq(parsedline->directive, gssymubanalyze, gssymcodeop, ".ubanalyze")) {
        if (*fields[0])
            gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
        else
            parsedline->label = 0;
        for (i = 2; i < n && strcmp(fields[i], "|"); i += 2) {
            if (i + 1 >= n)
                gsfatal(UNIMPL("%s:%d: missing code lable on final continuation to .ubanalyze"), filename, *plineno);
            parsedline->arguments[i - 2] = gsintern_string(gssymconstrlable, fields[i]);
            parsedline->arguments[i + 1 - 2] = gsintern_string(gssymcodelable, fields[i + 1]);
        }
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n && strcmp(fields[i], "|"); i ++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
        ;
    } else {
        return 0;
    }
    return 1;
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

        if (gsparse_code_type_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_arg_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_fv_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_thunk_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gssymeq(parsedline->directive, gssymcodeop, ".bind")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymdatalable, fields[0]);
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing code label", filename, *plineno);
            parsedline->arguments[2 - 2] = gsintern_string(gssymcodelable, fields[2]);
            for (i = 3; i < n && strcmp(fields[i], "|"); i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
            ;
            if (i < n) {
                parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
                i++;
            }
            for (; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
            ;
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
            if (i < n) {
                parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
                i++;
            }
            for (; i < n; i++)
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

static void gsparse_case(char *, gsparsedfile *, struct uxio_ichannel *, gsinterned_string, char *, int *, char **);
static void gsparse_default(char *, gsparsedfile *, struct uxio_ichannel *, char *, int *, char **);

static void gsparse_check_label_on_terminal_op(gsparsedfile *, struct gsparsedline *, char **);

static
int
gsparse_code_terminal_expr_op(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymundef, gssymyield, gssymenter, gssymubprim, gssymlprim, gsssymanalyze, gsssymdanalyze;
    int i;

    if (gssymceq(parsedline->directive, gssymundef, gssymcodeop, ".undef")) {
        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing type on .undef", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymtypelable, fields[2]);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
    } else if (gssymceq(parsedline->directive, gssymyield, gssymcodeop, ".yield")) {
        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing argument to .yield", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymdatalable, fields[2]);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
    } else if (gssymceq(parsedline->directive, gssymenter, gssymcodeop, ".enter")) {
        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing argument to .enter", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymdatalable, fields[2]);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
    } else if (gssymceq(parsedline->directive, gssymubprim, gssymcodeop, ".ubprim")) {
        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing primset on .ubprim", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymprimsetlable, fields[2]);
        if (n < 4)
            gsfatal("%s:%d: Missing prim name on .ubprim", filename, *plineno);
        parsedline->arguments[3 - 2] = gsintern_string(gssymdatalable, fields[3]);
        if (n < 5)
            gsfatal("%s:%d: Missing declared type on .ubprim", filename, *plineno);
        parsedline->arguments[4 - 2] = gsintern_string(gssymtypelable, fields[4]);
        for (i = 5; i < n && strcmp(fields[i], "|"); i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i]);
        }
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++) {
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i]);
        }
    } else if (gssymceq(parsedline->directive, gssymlprim, gssymcodeop, ".lprim")) {
        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing primset on .lprim", filename, *plineno)
        ;
        parsedline->arguments[2 - 2] = gsintern_string(gssymprimsetlable, fields[2]);
        if (n < 4)
            gsfatal("%s:%d: Missing prim name on .lprim", filename, *plineno)
        ;
        parsedline->arguments[3 - 2] = gsintern_string(gssymdatalable, fields[3]);
        if (n < 5)
            gsfatal("%s:%d: Missing declared type on .lprim", filename, *plineno)
        ;
        parsedline->arguments[4 - 2] = gsintern_string(gssymtypelable, fields[4]);
        for (i = 5; i < n && strcmp(fields[i], "|"); i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
        if (i < n) {
            parsedline->arguments[i - 2] = gsintern_string(gssymseparator, fields[i]);
            i++;
        }
        for (; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymdatalable, fields[i])
        ;
    } else if (gssymceq(parsedline->directive, gsssymanalyze, gssymcodeop, ".analyze")) {
        struct gsparsedline *p;
        int constrnum;

        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing scrutinee on .analyze", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymdatalable, fields[2]);
        if (n < 4)
            gsfatal("%s:%d: No constructors in .analyze (use .eanalyze for eliminating empty sums)", filename, *plineno);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymconstrlable, fields[i]);
        p = parsedline;
        for (constrnum = 0; 3 + constrnum < n; constrnum++) {
            gsparse_case(filename, parsedfile, chan, parsedline->arguments[1 + constrnum], line, plineno, fields);
        }
    } else if (gssymceq(parsedline->directive, gsssymdanalyze, gssymcodeop, ".danalyze")) {
        struct gsparsedline *p;
        int constrnum;

        gsparse_check_label_on_terminal_op(parsedfile, parsedline, fields);
        if (n < 3)
            gsfatal("%s:%d: Missing scrutinee on .danalyze", filename, *plineno);
        parsedline->arguments[2 - 2] = gsintern_string(gssymdatalable, fields[2]);
        if (n < 4)
            gsfatal("%s:%d: Huh.  No constructors in .danalyze.  Why not just go to the default directly?", filename, *plineno);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymconstrlable, fields[i])
        ;
        p = parsedline;
        gsparse_default(filename, parsedfile, chan, line, plineno, fields);
        for (constrnum = 0; 3 + constrnum < n; constrnum++) {
            gsparse_case(filename, parsedfile, chan, parsedline->arguments[1 + constrnum], line, plineno, fields)
        ;
        }
    } else {
        return 0;
    }
    return 1;
}

static
void
gsparse_check_label_on_terminal_op(gsparsedfile *parsedfile, struct gsparsedline *parsedline, char **fields)
{
    if (*fields[0])
        gsfatal("%P: Labels illegal on terminal ops", parsedline->pos);
    else
        parsedline->label = 0;
}

static int gsparse_cont_type_arg(char *, struct gsparsedline *, int *, char **, long);

static
void
gsparse_case(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, gsinterned_string constr, char *line, int *plineno, char **fields)
{
    static gsinterned_string gssymcase;

    struct gsparsedline *parsedline;
    long n;

    if ((n = gsgrabline(filename, chan, line, plineno, fields)) < 0)
        gsfatal("%s:%d: Error in reading API line: %r", filename, *plineno)
    ; else if (n == 0)
        gsfatal("%s:%d: EOF when looking for .case", filename, *plineno - 1)
    ;
    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

    parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

    if (!gssymceq(parsedline->directive, gssymcase, gssymcodeop, ".case"))
        gsfatal("%s:%d: Expecting .case", filename, *plineno);

    if (*fields[0])
        gsfatal("%s:%d: Labels illegal on .case");
    else
        parsedline->label = 0;

    if (n < 3)
        gsfatal("%s:%d: Missing constructor on .case", filename, *plineno);
    parsedline->arguments[2 - 2] = gsintern_string(gssymconstrlable, fields[2]);
    if (parsedline->arguments[2 - 2] != constr)
        gsfatal("%s:%d: Wrong constructor; expected %y but got %y", filename, *plineno, constr, parsedline->arguments[2 - 2]);

    if (n > 3)
        gsfatal("%s:%d: Too many arguments to .case", filename, *plineno);

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        if (gsparse_cont_type_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_type_let_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_field_cont_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_arg(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_thunk_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_value_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return;
        } else {
            gsfatal_unimpl(__FILE__, __LINE__, "%s:%d: Unimplemented .case op %y", filename, *plineno, parsedline->directive);
        }
    }
    gsfatal_unimpl(__FILE__, __LINE__, ".analyze: parse .case");
}

static
void
gsparse_default(char *filename, gsparsedfile *parsedfile, struct uxio_ichannel *chan, char *line, int *plineno, char **fields)
{
    static gsinterned_string gssymdefault;

    struct gsparsedline *parsedline;
    long n;

    if ((n = gsgrabline(filename, chan, line, plineno, fields)) < 0)
        gsfatal("%s:%d: Error in reading line: %r", filename, *plineno)
    ; else if (n == 0)
        gsfatal("%s:%d: EOF when looking for .default", filename, *plineno - 1)
    ;
    parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

    parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

    if (!gssymceq(parsedline->directive, gssymdefault, gssymcodeop, ".default"))
        gsfatal("%s:%d: Expecting .default", filename, *plineno)
    ;

    if (*fields[0]) gsfatal("%s:%d: Labels illegal on .default");
    else parsedline->label = 0;

    if (n > 2) gsfatal("%s:%d: Too many arguments to .default", filename, *plineno);

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymcodeop, fields[1]);

        if (gsparse_value_alloc_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_cont_push_op(filename, parsedline, plineno, fields, n)) {
        } else if (gsparse_code_terminal_expr_op(filename, parsedfile, chan, line, parsedline, plineno, fields, n)) {
            return;
        } else {
            gsfatal(UNIMPL("%s:%d: Unimplemented .default op %y"), filename, *plineno, parsedline->directive);
        }
    }
    gsfatal(UNIMPL(".danalyze: parse .default"));
}

static
int
gsparse_cont_type_arg(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymexkarg;

    if (gssymceq(parsedline->directive, gssymexkarg, gssymcodeop, ".exkarg")) {
        if (*fields[0]) {
            parsedline->label = gsintern_string(gssymtypelable, fields[0]);
        } else {
            gswarning("%P: Missing label on .exkarg", parsedline->pos);
            parsedline->label = 0;
        }
        if (n < 3)
            gsfatal("%P: Missing kind on .exkarg", parsedline->pos)
        ;
        parsedline->arguments[2 - 2] = gsintern_string(gssymkindexpr, fields[2]);
        if (n > 4)
            gsfatal("%P: Too many arguments to .exkarg", parsedline->pos)
        ;
    } else {
        return 0;
    }

    return 1;
}

static
int
gsparse_field_cont_arg(char *filename, struct gsparsedline *parsedline, int *plineno, char **fields, long n)
{
    static gsinterned_string gssymfkarg;

    int i;

    if (gssymceq(parsedline->directive, gssymfkarg, gssymcodeop, ".fkarg")) {
        if (*fields[0])
            parsedline->label = gsintern_string(gssymdatalable, fields[0]);
        else
            gsfatal("%P: Missing label on .fkarg", parsedline->pos);
        if (n < 3)
            gsfatal("%P: Missing field name on .fkarg", parsedline->pos);
        parsedline->arguments[2 - 2] = gsintern_string(gssymfieldlable, fields[2]);
        if (n < 4)
            gsfatal("%P: Missing type on .fkarg", parsedline->pos);
        for (i = 3; i < n; i++)
            parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
        ;
    } else {
        return 0;
    }

    return 1;
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
    } else if (gssymeq(parsedline->directive, gssymtypedirective, ".tyelimprim")) {
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
    } else if (gssymeq(parsedline->directive, gssymtypedirective, ".tyelimprim")) {
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
    static gsinterned_string gssymtyforall, gssymtyexists, gssymtylet, gssymtylift, gssymtyfun, gssymtyref, gssymtysum, gssymtyubsum, gssymtyproduct, gssymtyubproduct;

    struct gsparsedline *parsedline;
    int i;
    long n;

    while ((n = gsgrabline(filename, chan, line, plineno, fields)) > 0) {
        parsedline = gsparsed_file_addline(filename, parsedfile, *plineno, n);

        parsedline->directive = gsintern_string(gssymtypeop, fields[1]);

        if (gsparse_type_or_coercion_op(filename, parsedline, plineno, fields, n, gssymtypeop)) {
        } else if (gssymceq(parsedline->directive, gssymtyforall, gssymtypeop, ".tyforall")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0]);
            else
                gsfatal("%s:%d: Labels required on .tyforall", filename, *plineno);
            if (n < 3)
                gsfatal("%s:%d: Missing kind on .tyforall-bound type variable", filename, *plineno);
            parsedline->arguments[0] = gsintern_string(gssymkindexpr, fields[2]);
            if (n > 3)
                gsfatal("%s:%d: Too many arguments to .tyforall", filename, *plineno);
        } else if (gssymceq(parsedline->directive, gssymtyexists, gssymtypeop, ".tyexists")) {
            if (*fields[0])
                parsedline->label = gsintern_string(gssymtypelable, fields[0])
            ; else
                gsfatal("%s:%d: Labels required on .tyexists", filename, *plineno)
            ;
            if (n < 3)
                gsfatal("%s:%d: Missing kind on .tyexists-bound type variable", filename, *plineno)
            ;
            parsedline->arguments[0] = gsintern_string(gssymkindexpr, fields[2]);
            if (n > 3)
                gsfatal("%s:%d: Too many arguments to .tyexists", filename, *plineno)
            ;
        } else if (gssymceq(parsedline->directive, gssymtylet, gssymtypeop, ".tylet")) {
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
        } else if (gssymceq(parsedline->directive, gssymtylift, gssymtypeop, ".tylift")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n > 2)
                gsfatal("%s:%d: Too many arguments to .tylift", filename, *plineno);
        } else if (gssymceq(parsedline->directive, gssymtyfun, gssymtypeop, ".tyfun")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on continuation ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n < 3)
                gsfatal("%s:%d: Missing argument type to .tyfun", filename, *plineno);
            for (i = 2; i < n; i++)
                parsedline->arguments[i - 2] = gsintern_string(gssymtypelable, fields[i])
            ;
        } else if (gssymceq(parsedline->directive, gssymtyref, gssymtypeop, ".tyref")) {
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
        } else if (gssymceq(parsedline->directive, gssymtysum, gssymtypeop, ".tysum")) {
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
        } else if (gssymceq(parsedline->directive, gssymtyubsum, gssymtypeop, ".tyubsum")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n % 2)
                gsfatal("%s:%d: Can't have odd number of arguments to .tyubsum", filename, *plineno);
            for (i = 0; 2 + i < n; i += 2) {
                parsedline->arguments[i] = gsintern_string(gssymconstrlable, fields[2 + i]);
                parsedline->arguments[i + 1] = gsintern_string(gssymtypelable, fields[2 + i + 1]);
            }
            return 0;
        } else if (gssymceq(parsedline->directive, gssymtyproduct, gssymtypeop, ".typroduct")) {
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
        } else if (gssymceq(parsedline->directive, gssymtyubproduct, gssymtypeop, ".tyubproduct")) {
            if (*fields[0])
                gsfatal("%s:%d: Labels illegal on terminal ops", filename, *plineno);
            else
                parsedline->label = 0;
            if (n % 2)
                gsfatal("%s:%d: Can't have odd number of arguments to .tyubproduct", filename, *plineno);
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
        if ((n = gsbio_device_getline(chan, line, LINE_LENGTH)) < 0)
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
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Symbol tables",
};

static void *symtable_nursury;

static struct gs_block_class gssymtable_item_segment = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
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
    gsappend_items(&symtable0->coercionitems, symtable1->coercionitems, "coercion");
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
            gsfatal("%P: Duplicate coercion item %y (duplicate of %P)", ptype->pos, label, (*p)->value->pos)
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
    /* indirection_dereferencer = */ gsnoindir,
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
    /* indirection_dereferencer = */ gsnoindir,
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
    /* indirection_dereferencer = */ gsnoindir,
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
gssymtable_lookup(struct gspos pos, struct gsfile_symtable *symtable, gsinterned_string label)
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

    gsfatal("%P: Unknown %s '%s'", pos, strtype, label->name);

    return res;
}

struct gsfile_symtable_scc_item {
    struct gsbc_item key;
    struct gsbc_scc *value;
    struct gsfile_symtable_scc_item *next;
};

static struct gs_block_class gsfile_symtable_scc_item_descr = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
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
        return gsbio_device_iopen(filename, omode);
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

    return gsbio_device_iclose(chan);
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
        p++
    ;
    label_present = p > line;
    if (*p && *p != '#')
        *p++ = 0
    ;

    pfield = fields + 1;
    while (*p && *p != '#' && pfield < fields + maxfields) {
        while (*p && isspace(*p) && *p != '#')
            p++
        ;
        if (*p && *p != '#') {
            *pfield++ = p;
            while (*p && !isspace(*p) && *p != '#')
                p++
            ;
            if (*p) *p++ = 0;
            numfields++;
        }
    }
    if (*p) *p++ = 0;
    return label_present || numfields ? numfields + 1 : 0;
}

/* Loader */

static void gsload_scc(gsparsedfile *, struct gsfile_symtable *, struct gsbc_scc *, struct gspos *, gsvalue *, struct gstype **);

void
gsloadfile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, struct gspos *pentrypos, gsvalue *pentry, struct gstype **ptype)
{
    struct gsbc_scc *pscc, *p;
    int contains_entry;

    switch (parsedfile->type) {
        case gsfileprefix:
            pscc = gsbc_topsortfile(parsedfile, symtable);
            for (p = pscc; p; p = p->next_scc) {
                gsload_scc(parsedfile, symtable, p, 0, 0, 0);
            }
            return;
        case gsfiledocument:
            pscc = gsbc_topsortfile(parsedfile, symtable);
            for (p = pscc; p; p = p->next_scc) {
                contains_entry = !p->next_scc;
                gsload_scc(parsedfile, symtable, p, contains_entry ? pentrypos : 0, contains_entry ? pentry : 0, contains_entry ? ptype : 0);
            }
            return;
        default:
            gsfatal("%s: Unknown file type %d in gsbytecompile", parsedfile->name->name, parsedfile->type);
    }
}

static
void
gsload_scc(gsparsedfile *parsedfile, struct gsfile_symtable *symtable, struct gsbc_scc *pscc, struct gspos *pentrypos, gsvalue *pentry, struct gstype **ptype)
{
    struct gsbc_scc *p;
    struct gsbc_item items[MAX_ITEMS_PER_SCC];
    struct gstype *types[MAX_ITEMS_PER_SCC], *defns[MAX_ITEMS_PER_SCC];
    struct gskind *kinds[MAX_ITEMS_PER_SCC];
    gsvalue heap[MAX_ITEMS_PER_SCC];
    struct gsbco *bcos[MAX_ITEMS_PER_SCC];
    int n, i;

    n = 0;

    for (p = pscc; p; p = p->next_item) {
        if (n >= MAX_ITEMS_PER_SCC)
            gsfatal("%P: Too many items in this SCC; max 0x%x", p->item.v->pos, MAX_ITEMS_PER_SCC)
        ;
        items[n++] = p->item;
    }

    /* §section{Type-checking} */

    gstypes_process_type_declarations(symtable, items, kinds, n);
    gstypes_compile_types(symtable, items, types, n);
    gstypes_compile_type_definitions(symtable, items, defns, n);
    gstypes_kind_check_scc(symtable, items, types, defns, kinds, n);
    gstypes_process_type_signatures(symtable, items, ptype, n);
    gstypes_type_check_scc(symtable, items, types, kinds, ptype, n);

    /* §section{Byte-compilation} */

    gsbc_alloc_data_for_scc(symtable, items, heap, n);
    gsbc_alloc_code_for_scc(symtable, items, bcos, n);
    gsbc_bytecompile_scc(symtable, items, heap, bcos, n);

    if (pentry) {
        for (i = 0; i < n; i++) {
            if (
                items[i].type == gssymdatalable
                && items[i].v == GSDATA_SECTION_FIRST_ITEM(parsedfile->data)
            ) {
                *pentrypos = items[i].v->pos;
                if (heap[i])
                    *pentry = heap[i];
                else
                    gsfatal_unimpl(__FILE__, __LINE__, "%s: Entry point: couldn't find in any SCC");
                if (items[i].v->label) {
                    gsfatal_unimpl(__FILE__, __LINE__, "%P: set *ptype", items[i].v->pos);
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
