/* §section Continuations */

struct gsbc_cont {
    enum {
        gsbc_cont_update,
        gsbc_cont_app,
        gsbc_cont_force,
        gsbc_cont_strict,
        gsbc_cont_ubanalyze,
    } node;
    struct gspos pos;
};

struct gsbc_cont_update {
    struct gsbc_cont cont;
    struct gsheap_item *dest;
    struct gsbc_cont_update *next;
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

/* §section ACE Stack manipulations */

struct gsbc_cont *ace_stack_alloc(struct ace_thread *, struct gspos, ulong); /* Deprecated */

struct gsbc_cont_update *ace_push_update(struct gspos, struct ace_thread *, struct gsheap_item *);
struct gsbc_cont_app *ace_push_appv(struct gspos, struct ace_thread *, int, gsvalue *);

struct gsbc_cont *ace_stack_top(struct ace_thread *);

void ace_pop_update(struct ace_thread *);

/* §section Global Script Closures & Indirections */

void gsheap_lock(struct gsheap_item *);
void gsheap_unlock(struct gsheap_item *);

void gsblackhole_heap(struct gsheap_item *, struct gsbc_cont_update *);

void gsupdate_heap(struct gsheap_item *, gsvalue);
gstypecode gsheapstate(struct gspos, struct gsheap_item *);

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
