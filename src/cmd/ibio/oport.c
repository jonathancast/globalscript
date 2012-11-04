#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

#include "ibio.h"

static struct ibio_oport *ibio_alloc_oport(void);

gsvalue
ibio_oport_fdopen(int fd, char *err, char *eerr)
{
    struct ibio_oport *res;

    res = ibio_alloc_oport();

    /* Create write process tied to res */

    return (gsvalue)res;
}

struct ibio_oport {
};

static struct gs_block_class ibio_oport_segment_descr = {
    /* evaluator = */ gswhnfeval,
    /* description = */ "IBIO Oports",
};
static void *ibio_oport_segment_nursury;
static Lock ibio_oport_segment_lock;

static
struct ibio_oport *
ibio_alloc_oport()
{
    struct ibio_oport *res;

    lock(&ibio_oport_segment_lock);
    res = gs_sys_seg_suballoc(&ibio_oport_segment_descr, &ibio_oport_segment_nursury, sizeof(*res), sizeof(void*));
    lock(&ibio_oport_segment_lock);

    /* Output channel (linked list of segments) can be created dynamically */

    /* Put oport on list of write threads */

    return res;
}

