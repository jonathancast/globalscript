#define NOPLAN9DEFINES 1

#ifdef  __linux__
#define _GNU_SOURCE 1
#endif

#include <u.h>
#include <libc.h>
#include <stdatomic.h>

#include <sys/mman.h>

#include <libglobalscript.h>

#include "../gsmem.h"

#define GSPROT_HEAP (PROT_READ | PROT_WRITE)
#define GSMAP_HEAP (MAP_ANONYMOUS | MAP_PRIVATE)
/* These two are for portability, but defined here to make the code look nicer */
#define GSFD_HEAP (-1)
#define GSOFFSET_HEAP (0)

void
gs_sys_alloc_blocks(ulong num_blocks, void **pbot, void **ptop)
{
    void *sysmem, *sysend;
    size_t sz, syssz;
    int res;

    sz = num_blocks * BLOCK_SIZE;
    /* Allocate extra memory to ensure we can block-align the result */
    syssz = sz + BLOCK_SIZE;

    sysmem = mmap(0, syssz, GSPROT_HEAP, GSMAP_HEAP, GSFD_HEAP, GSOFFSET_HEAP);
    if (sysmem == (void*)-1) gsfatal("Cannot allocate 0x%ulx blocks: %r", num_blocks);
    sysend = (uchar*)sysmem + sz;

    /* Force sysmem to be properly-aligned */
    if ((uintptr)sysmem % BLOCK_SIZE) {
        void *rounded_up = (uchar*)sysmem + (BLOCK_SIZE - (uintptr)sysmem % BLOCK_SIZE);
        res = munmap(sysmem, (uchar*)rounded_up - (uchar*)sysmem);
        if (res < 0) gsfatal(UNIMPL("munmap failed: %r"));
        sysmem = rounded_up;
    }

    /* We must have allocated too much memory at the end, but check anyway */
    if ((uchar*)sysend > (uchar*)sysmem + sz) {
        void *rounded_down = (uchar*)sysmem + sz;
        gswarning(UNIMPL("rounded_down = %p, sysend = %p"), rounded_down, sysend);
        res = munmap(rounded_down, (uchar*)sysend - (uchar*)rounded_down);
        if (res < 0) gsfatal(UNIMPL("munmap failed: %r"));
        sysend = rounded_down;
    }

    *pbot = sysmem;
    *ptop = sysend;
}
