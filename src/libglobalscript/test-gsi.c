#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

static int gsprint(gsvalue prog);

void
gsrun(gsvalue prog)
{
    gstypecode st;

    st = GS_SLOW_EVALUATE(prog);

    switch (st) {
        case gstywhnf:
            if (gsprint(prog) < 0)
                exits("error");
            return;
        default:
            gsfatal_unimpl(__FILE__, __LINE__, "gsrun: st = %d", st);
    }

    gsfatal("%s:%d: gsrun next", __FILE__, __LINE__);
}

static
int
gsprint(gsvalue prog)
{
    struct gs_blockdesc *block;

    block = BLOCK_CONTAINING(prog);

    if (gsiserror_block(block)) {
        struct gserror *p;

        p = (struct gserror *)prog;
        print("%s %s:%d\n", "undefined", p->file->name, p->lineno);
        return -1;
    } else {
        gsfatal_unimpl(__FILE__, __LINE__, "gsprint(%s)", block->class->description);
        return -1;
    }
}
