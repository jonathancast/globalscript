/* §section Continuations */

struct gsbc_cont {
    enum {
        gsbc_cont_app,
        gsbc_cont_force,
        gsbc_cont_strict,
        gsbc_cont_ubanalyze,
    } node;
    struct gspos pos;
};

struct gsbc_cont_app {
    struct gsbc_cont cont;
    int numargs;
    gsvalue arguments[];
};

struct gsbc_cont_force {
    struct gsbc_cont cont;
    struct gsbco *code;
    int numfvs;
    gsvalue fvs[];
};

struct gsbc_cont_strict {
    struct gsbc_cont cont;
    struct gsbco *code;
    int numfvs;
    gsvalue fvs[];
};

struct gsbc_cont_ubanalyze {
    struct gsbc_cont cont;
    int numconts;
    struct gsbco **conts;
    int numfvs;
    gsvalue *fvs;
};

/* §section Global Script Run-time Errors */

void *gsreserveerrors(ulong);

void gspoison(struct gsheap_item *, struct gspos, char *, ...);
void gspoison_unimpl(struct gsheap_item *, char *, int, struct gspos, char *, ...);

/* §section Global Script Implementation Errors */

void *gsreserveimplementation_failures(ulong);

/* §section Records */

void *gsreserverecords(ulong);

/* §section Field extraction thunks */

gsvalue gslfield(struct gspos, int, gsvalue);

/* §section Constructors */

void *gsreserveconstrs(ulong);

/* §section API Primitives */

void *gsreserveeprims(ulong);
