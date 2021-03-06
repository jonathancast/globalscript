/* §source.file{Allocating & Managing Parsed Source Files} */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>
#include "gsinputfile.h"
#include "gsinputalloc.h"

static struct gs_blockdesc *gsalloc_new_parsed_file_block(void);

/* §section Allocating a New File */

gsparsedfile *
gsparsed_file_alloc(char *filename, char *relname, gsfiletype type, uint features)
{
    struct gs_blockdesc *parsed_file_nursury_seg;
    gsparsedfile *pres;

    parsed_file_nursury_seg = gsalloc_new_parsed_file_block();

    pres = (gsparsedfile *)START_OF_BLOCK(parsed_file_nursury_seg);
    pres->size = sizeof(gsparsedfile);
    pres->name = gsintern_string(gssymfilename, filename);
    pres->relname = gsintern_string(gssymfilename, relname);
    pres->type = type;
    pres->features = features;
    pres->data = 0;
    pres->code = 0;
    pres->types = 0;
    pres->coercions = 0;
    pres->last_seg = &pres->first_seg;
    pres->first_seg.extent = (uchar*)pres + sizeof(gsparsedfile);
    pres->first_seg.next = 0;

    return pres;
}

/* §section Allocating Memory from a Parsed File */

struct gs_block_class gsparsed_file_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* gc_trace = */ gsunimplgc,
    /* description = */ "Parsed gsac files",
};

static
struct gs_blockdesc *
gsalloc_new_parsed_file_block()
{
    return gs_sys_block_alloc(&gsparsed_file_desc);
}

static void gsparsed_file_add_segment(gsparsedfile *parsedfile);

/* ↓ It is part of the interface of this file that §c{parsedfile->last_seg} contains the allocated memory. */
void *
gsparsed_file_extend(gsparsedfile *parsedfile, ulong n)
{
    struct input_block *nursury_seg;
    void *res;
    struct gsparsedfile_segment *seg;

    seg = parsedfile->last_seg;
    nursury_seg = BLOCK_CONTAINING(seg->extent);
    if ((uchar*)END_OF_BLOCK(nursury_seg) - (uchar*)seg->extent < n) {
        gsparsed_file_add_segment(parsedfile);
        seg = parsedfile->last_seg;
        nursury_seg = BLOCK_CONTAINING(seg->extent);
    }

    res = seg->extent;

    seg->extent = (uchar*)seg->extent + n;
    if ((uintptr)seg->extent % sizeof(void*))
        seg->extent = (uchar*)seg->extent + sizeof(void*) - ((uintptr)seg->extent % sizeof(void*));

    return res;
}

void
gsparsed_file_add_segment(gsparsedfile *parsedfile)
{
    struct gs_blockdesc *nursury_seg;
    struct gsparsedfile_segment *new_segment;

    gsfatal("%s:%d: %s: Pretty sure multi-segment files don't actually work yet", __FILE__, __LINE__, parsedfile->name->name);

    nursury_seg = gs_sys_block_alloc(&gsparsed_file_desc);
    new_segment = START_OF_BLOCK(nursury_seg);
    new_segment->extent = (uchar*)new_segment + sizeof(*new_segment);
    new_segment->next = 0;

    parsedfile->last_seg->next = new_segment;
    parsedfile->last_seg = new_segment;
}

/* §subsection{Specifically Appending Lines to Source Files} */

/* ↓ It is part of the interface of this file that §c{parsedfile->last_seg} contains the returned line. */
struct gsparsedline *
gsparsed_file_addline(struct gsparse_input_pos *pos, gsparsedfile *parsedfile, ulong numfields)
{
    ulong size;
    struct gsparsedline *res;

    if (numfields < 2)
        gsfatal("%s:%d: Missing directive", pos->real_filename, pos->real_lineno)
    ;

    size = sizeof(*res) + sizeof(gsinterned_string) * (numfields - 2);
    res = gsparsed_file_extend(parsedfile, size);

    res->pos = pos->artificial;
    res->numarguments = numfields - 2;

    return res;
}

/* §section Manipulating Parsed Source Files */

struct gsparsedline *
gsinput_next_line(struct gsparsedfile_segment **ppseg, struct gsparsedline *p)
{
    struct gsparsedline *pres;

    pres = (struct gsparsedline *)((uchar*)p + sizeof(*p) + p->numarguments * sizeof(gsinterned_string));

    if ((uchar*)pres >= (uchar*)(*ppseg)->extent)
        gsfatal(UNIMPL("%P: Next line when changing segments"), p->pos);

    return pres;
}

/* §section Syntax-checking and complaining about source files */

void
gsargcheck(struct gsparsedline *inpline, ulong argnum, char *fmt, ...)
{
    char buf[0x100];
    va_list arg;

    if (inpline->numarguments > argnum)
        return
    ;

    va_start(arg, fmt);
    vseprint(buf, buf+sizeof buf, fmt, arg);
    va_end(arg);

    gsfatal("%P: Missing argument %s to %s", inpline->pos, buf, inpline->directive->name);
}
