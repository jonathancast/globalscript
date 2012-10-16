/* §section{(Byte-Code) Code Segment} */

struct gsbco {
    enum {
        gsbc_expr,
    } tag;
    gsinterned_string file;
    int lineno;
    ulong size;
    ulong numglobals;
};

struct gsbc {
    gsinterned_string file;
    int lineno;
    uchar instr;
    uchar args[];
};

enum {
    gsbc_op_undef,
    gsbc_op_enter,
};

#define GS_NTH_ARG_OFFSET(n) (offsetof(struct gsbc, args) + n)

#define GS_SIZE_BYTECODE(n) (GS_NTH_ARG_OFFSET(n) + sizeof(gsinterned_string) - GS_NTH_ARG_OFFSET(n) % sizeof(gsinterned_string))
#define GS_NEXT_BYTECODE(p, n) ((struct gsbc *)((uchar*)p + GS_SIZE_BYTECODE(n)))

void *gsreservebytecode(ulong);

void *gsreserveerrors(ulong);

struct gserror *gserror(gsinterned_string, int, char *, ...);
struct gserror *gserror_unimpl(char *, int, gsinterned_string, int, char *, ...);

void gspoison(struct gsheap_item *, gsinterned_string, int, char *, ...);
void gspoison_unimpl(struct gsheap_item *, char *, int, gsinterned_string, int, char *, ...);