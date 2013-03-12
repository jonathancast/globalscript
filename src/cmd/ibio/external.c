/* §source.file External File I/O */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

static struct ibio_external_io *ibio_alloc_external_io(long, ibio_external_canread *, ibio_external_readsym *);

/* §section Runes */

int
ibio_prim_external_io_handle_rune(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args, gsvalue *pres)
{
    struct ibio_external_io *res;

    res = ibio_rune_io();

    *pres = (gsvalue)res;
    return 1;
}

static struct ibio_external_io *ibio_rune_io_value;
static Lock ibio_rune_io_lock;
static gs_sys_gc_root_callback ibio_rune_io_callback;

static ibio_external_canread ibio_rune_canread;
static ibio_external_readsym ibio_rune_readsym;

struct ibio_external_io *
ibio_rune_io()
{
    lock(&ibio_rune_io_lock);
    if (ibio_rune_io_value) {
        unlock(&ibio_rune_io_lock);
        return ibio_rune_io_value;
    }

    ibio_rune_io_value = ibio_alloc_external_io(sizeof(*ibio_rune_io_value), ibio_rune_canread, ibio_rune_readsym);

    unlock(&ibio_rune_io_lock);

    gs_sys_gc_root_callback_register(ibio_rune_io_callback);

    return ibio_rune_io_value;
}

int
ibio_rune_io_callback(struct gsstringbuilder *err)
{
    gsstring_builder_print(err, UNIMPL("ibio_rune_io_callback"));
    return -1;
}

static
int
ibio_rune_canread(struct ibio_external_io *io, void *start, void *end)
{
    uchar fst;

    if ((uchar*)start >= (uchar*)end)
        return 0
    ;
    fst = *(uchar*)start;
    if (fst < 0x80)
        return (uchar*)start <= (uchar*)end - 1
    ; else if (fst < 0xc0)
        return (uchar*)start <= (uchar*)end - 2
    ; else if (fst < 0xe0)
        return (uchar*)start <= (uchar*)end - 3
    ; else
        return (uchar*)start <= (uchar*)end - 4
    ;
}

static
void *
ibio_rune_readsym(struct ibio_external_io *io, char *err, char *eerr, void *start, void *end, gsvalue *pres)
{
    return gschartorune((char*)start, pres, err, eerr);
}

/* §section Directories */

int
ibio_prim_external_io_handle_dir(struct ace_thread *thread, struct gspos pos, int nargs, gsvalue *args, gsvalue *pres)
{
    struct ibio_external_io *res;

    res = ibio_dir_io(pos, args[0]);

    *pres = (gsvalue)res;
    return 1;
}

struct ibio_external_dir_io {
    struct ibio_external_io io;
    struct gspos pos;
    gsvalue dirfromprim;
};

static ibio_external_canread ibio_dir_canread;
static ibio_external_readsym ibio_dir_readsym;

struct ibio_external_io *
ibio_dir_io(struct gspos pos, gsvalue v)
{
    struct ibio_external_dir_io *res;

    res = (struct ibio_external_dir_io *)ibio_alloc_external_io(sizeof(*res), ibio_dir_canread, ibio_dir_readsym);
    res->pos = pos;
    res->dirfromprim = v;

    return (struct ibio_external_io *)res;
}

#define GET_LITTLE_ENDIAN_U16INT(p) ((u16int)*(uchar*)(p) + ((u16int)*((uchar*)(p)+1)<<8))

static
int ibio_dir_canread(struct ibio_external_io *io, void *start, void *end)
{
    u16int sz;

    if ((uchar*)start + 2 > (uchar*)end)
        return 0
    ;

    sz = GET_LITTLE_ENDIAN_U16INT(start);
    return (uchar*)start + 2 + sz <= (uchar*)end;
}

static
void *
ibio_dir_readsym(struct ibio_external_io *io, char *err, char *eerr, void *start, void *end, gsvalue *pres)
{
    u16int sz;
    struct gsbio_dir *dir;
    gsvalue pd;
    struct ibio_external_dir_io *dio;

    dio = (struct ibio_external_dir_io *)io;

    if ((uchar*)end - (uchar*)start < 2) {
        seprint(err, eerr, "Not enough data to read a size");
        return 0;
    }

    sz = GET_LITTLE_ENDIAN_U16INT(start);

    if ((uchar*)end - (uchar*)start < 2 + sz) {
        seprint(err, eerr, "Not enough data in buffer; need 0x%xB but have 0x%xB", 2 + sz, (uchar*)end - (uchar*)start);
        return 0;
    }

    dir = gsbio_parse_stat(sz, (uchar*)start + 2);
    pd = ibio_parse_gsbio_dir(dio->pos, dir);

    *pres = gsapply(dio->pos, dio->dirfromprim, pd);
    return (uchar*)start + 2 + sz;
}

/* §section Allocation */

static struct gs_sys_global_block_suballoc_info ibio_external_io_info = {
    /* descr = */ {
        /* evaluator = */ gswhnfeval,
        /* indirection_dereferencer = */ gswhnfindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "External I/O descriptors",
    },
};

static
struct ibio_external_io *
ibio_alloc_external_io(long sz, ibio_external_canread *canread, ibio_external_readsym *readsym)
{
    struct ibio_external_io *res;

    res = gs_sys_global_block_suballoc(&ibio_external_io_info, sz);
    res->canread = canread;
    res->readsym = readsym;

    return res;
}
