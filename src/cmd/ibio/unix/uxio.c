/* §source.file Unix System Call-Level File I/O */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "../ibio.h"

static struct ibio_uxio *ibio_alloc_uxio(ulong, ibio_uxio_refill *, ibio_uxio_gccopy *, ibio_uxio_gcevacuate *);

/* §section Files */

static struct ibio_uxio *ibio_file_uxio_value;
static Lock ibio_file_uxio_lock;
static gs_sys_gc_root_callback ibio_file_uxio_callback;

static ibio_uxio_refill ibio_file_refill;
static ibio_uxio_gccopy ibio_file_gccopy;
static ibio_uxio_gcevacuate ibio_file_gcevacuate;

struct ibio_uxio *
ibio_file_uxio()
{
    lock(&ibio_file_uxio_lock);
    if (ibio_file_uxio_value) {
        unlock(&ibio_file_uxio_lock);
        return ibio_file_uxio_value;
    }

    ibio_file_uxio_value = ibio_alloc_uxio(sizeof(*ibio_file_uxio_value), ibio_file_refill, ibio_file_gccopy, ibio_file_gcevacuate);

    unlock(&ibio_file_uxio_lock);

    gs_sys_gc_root_callback_register(ibio_file_uxio_callback);

    return ibio_file_uxio_value;
}

int
ibio_file_uxio_callback(struct gsstringbuilder *err)
{
    if (ibio_uxio_trace(err, &ibio_file_uxio_value) < 0) return -1;
    return 0;
}

long
ibio_file_refill(struct ibio_uxio *uxio, int fd, void *buf, long n)
{
    return read(fd, buf, n);
}

static
struct ibio_uxio *
ibio_file_gccopy(struct gsstringbuilder *err, struct ibio_uxio *uxio)
{
    return ibio_alloc_uxio(sizeof(*ibio_file_uxio_value), ibio_file_refill, ibio_file_gccopy, ibio_file_gcevacuate);
}

static
int
ibio_file_gcevacuate(struct gsstringbuilder *err, struct ibio_uxio *uxio)
{
    return 0;
}

/* §section Directories */

static ibio_uxio_refill ibio_dir_refill;
static ibio_uxio_gccopy ibio_dir_gccopy;
static ibio_uxio_gcevacuate ibio_dir_gcevacuate;

#define DIR_SEGMENT_SIZE 0x4000

struct ibio_dir_uxio {
    struct ibio_uxio uxio;
    char *dirname;
    void *bufbeg, *bufend;
    vlong offset;
};

struct ibio_uxio *
ibio_dir_uxio(char *dirname)
{
    struct ibio_dir_uxio *res;

    res = (struct ibio_dir_uxio *)ibio_alloc_uxio(DIR_SEGMENT_SIZE, ibio_dir_refill, ibio_dir_gccopy, ibio_dir_gcevacuate);
    res->dirname = (char*)res + sizeof(*res);
    strecpy(res->dirname, (char*)res + DIR_SEGMENT_SIZE, dirname);
    res->bufend = res->bufbeg = (uchar*)res + sizeof(*res) + strlen(dirname) + 1;
    res->offset = 0;

    return (struct ibio_uxio *)res;
}

long
ibio_dir_refill(struct ibio_uxio *uxio, int fd, void *buf, long n)
{
    struct ibio_dir_uxio *dirio;
    void *newbuf, *newdirbuf;
    long res;

    dirio = (struct ibio_dir_uxio *)uxio;

    if (dirio->bufbeg == dirio->bufend) {
        dirio->bufbeg = (uchar*)dirio + sizeof(*dirio) + strlen(dirio->dirname) + 1;
        res = gsbio_unix_read_directory(fd, dirio->bufbeg, (uchar*)dirio + DIR_SEGMENT_SIZE, &dirio->offset);
        if (res <= 0)
            return res
        ;
        dirio->bufend = (uchar*)dirio->bufbeg + res;
    }

    gsbio_unix_parse_directory(dirio->dirname, buf, (char*)buf + n, &newbuf, dirio->bufbeg, dirio->bufend, &newdirbuf);
    if (newbuf) {
        dirio->bufbeg = newdirbuf;
        return (uchar*)newbuf - (uchar*)buf;
    } else {
        return -1;
    }
}

static
struct ibio_uxio *
ibio_dir_gccopy(struct gsstringbuilder *err, struct ibio_uxio *uxio)
{
    gsstring_builder_print(err, UNIMPL("ibio_dir_gccopy"));
    return 0;
}

static
int
ibio_dir_gcevacuate(struct gsstringbuilder *err, struct ibio_uxio *uxio)
{
    gsstring_builder_print(err, UNIMPL("ibio_dir_gcevacuate"));
    return -1;
}

/* §section Allocation */

static struct gs_sys_global_block_suballoc_info ibio_uxio_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Unix system call-level file I/O descriptors",
    },
};

static
struct ibio_uxio *
ibio_alloc_uxio(ulong sz, ibio_uxio_refill *refill, ibio_uxio_gccopy *gccopy, ibio_uxio_gcevacuate *gcevacuate)
{
    struct ibio_uxio *res;

    res = gs_sys_global_block_suballoc(&ibio_uxio_info, sz);
    res->refill = refill;
    res->gccopy = gccopy;
    res->gcevacuate = gcevacuate;
    res->forward = 0;

    return res;
}

int
ibio_uxio_trace(struct gsstringbuilder *err, struct ibio_uxio **puxio)
{
    struct ibio_uxio *uxio, *newuxio;

    uxio = *puxio;
    if (uxio->forward) {
        *puxio = uxio->forward;
        return 0;
    }

    if (!gs_sys_block_in_gc_from_space(uxio)) return 0;

    if (!(newuxio = uxio->gccopy(err, uxio))) return -1;

    uxio->forward = *puxio = newuxio;

    if (newuxio->gcevacuate(err, newuxio) < 0) return -1;

    return 0;
}
