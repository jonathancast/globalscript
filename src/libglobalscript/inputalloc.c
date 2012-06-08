/* §source.file{Allocating & Managing Parsed Source Files} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

struct input_block {
    struct gs_blockdesc hdr;
};

static void *parsed_file_nursury;

static void gsalloc_new_parsed_file_block(void);

/* §section{Allocating a New File} */

gsparsedfile *
gsparsed_file_alloc(char *filename, char *relname, gsfiletype type)
{
    struct input_block *parsed_file_nursury_seg;
    gsparsedfile *pres;

    if (!parsed_file_nursury)
        gsalloc_new_parsed_file_block();

    parsed_file_nursury_seg = BLOCK_CONTAINING(parsed_file_nursury);
    if ((uchar*)END_OF_BLOCK(parsed_file_nursury_seg) - (uchar*)parsed_file_nursury < sizeof(gsparsedfile))
        gsalloc_new_parsed_file_block();
    pres = (gsparsedfile *)parsed_file_nursury;
    pres->size = sizeof(gsparsedfile);
    pres->extent = (uchar*)parsed_file_nursury + sizeof(gsparsedfile);
    pres->name = gsintern_string(gssymfilename, filename);
    pres->relname = gsintern_string(gssymfilename, relname);
    pres->type = type;
    pres->data = 0;
    pres->code = 0;
    pres->first_seg.next = 0;
    parsed_file_nursury = 0;

    return pres;
}

/* §section{Allocating Memory from a Parsed File} */

struct gs_block_class gsparsed_file_desc = {
    /* evaluator = */ gsnoeval,
    /* description = */ "Parsed gsac files",
};

static
void
gsalloc_new_parsed_file_block()
{
    struct input_block *nursury_seg;

    nursury_seg = gs_sys_seg_alloc(&gsparsed_file_desc);
    parsed_file_nursury = (void*)((uchar*)nursury_seg + sizeof(*nursury_seg));
    gsassert(__FILE__, __LINE__, !((uintptr)parsed_file_nursury % sizeof(ulong)), "parsed_file_nursury not ulong-aligned; check sizeof(struct input_block)");
}

static void gsparsed_file_add_segment(gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg);

void *
gsparsed_file_extend(gsparsedfile *parsedfile, ulong n, struct gsparsedfile_segment **ppseg)
{
    struct input_block *nursury_seg;
    void *res;

    nursury_seg = BLOCK_CONTAINING(parsedfile->extent);
    if ((uchar*)END_OF_BLOCK(nursury_seg) - (uchar*)parsedfile->extent < n) {
        gsparsed_file_add_segment(parsedfile, ppseg);
        nursury_seg = BLOCK_CONTAINING(parsedfile->extent);
    }

    res = parsedfile->extent;

    parsedfile->extent = (uchar*)parsedfile->extent + n;
    if ((uintptr)parsedfile->extent % sizeof(void*))
        parsedfile->extent = (uchar*)parsedfile->extent + sizeof(void*) - ((uintptr)parsedfile->extent % sizeof(void*));
    if ((uchar*)END_OF_BLOCK(nursury_seg) < (uchar*)parsedfile->extent)
        gsparsed_file_add_segment(parsedfile, ppseg);

    return res;
}

void
gsparsed_file_add_segment(gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg)
{
    struct input_block *nursury_seg;
    struct gsparsedfile_segment *new_segment;

    nursury_seg = gs_sys_seg_alloc(&gsparsed_file_desc);
    new_segment = (void*)(uchar*)nursury_seg + sizeof(*nursury_seg);
    new_segment->next = 0;

    parsedfile->extent = (uchar*)new_segment + sizeof(*new_segment);
    (*ppseg)->next = new_segment;
    *ppseg = new_segment;
}

/* §subsection{Specifically Appending Lines to Source Files} */

struct gsparsedline *
gsparsed_file_addline(char *filename, gsparsedfile *parsedfile, struct gsparsedfile_segment **ppseg, int lineno, ulong numfields)
{
    ulong size;
    struct gsparsedline *res;

    if (numfields < 2)
        gsfatal("%s:%d: Missing directive", filename, lineno);

    size = sizeof(*res) + sizeof(gsinterned_string) * (numfields - 2);
    res = gsparsed_file_extend(parsedfile, size, ppseg);

    res->file = parsedfile->name;
    res->lineno = lineno;
    res->numarguments = numfields - 2;

    return res;
}

/* §section{Manipulating Parsed Source Files} */

struct gsparsedline *
gsinput_next_line(struct gsparsedline *p)
{
    return (struct gsparsedline *)((uchar*)p + sizeof(*p) + p->numarguments * sizeof(gsinterned_string));
}
