/* §section{(Byte-Code) Code Segment} */

struct gsbc {
    struct gspos pos;
    uchar instr;
    uchar args[];
};

enum {
    gsbc_op_record,
    gsbc_op_unknown_eprim,
    gsbc_op_eprim,
    gsbc_op_undef,
    gsbc_op_enter,
    gsbc_op_yield,
    gsbc_op_bind,
    gsbc_op_body,
};

#define GS_NTH_ARG_OFFSET(n) (offsetof(struct gsbc, args) + n)

#define GS_SIZE_BYTECODE(n) (GS_NTH_ARG_OFFSET(n) + sizeof(gsinterned_string) - GS_NTH_ARG_OFFSET(n) % sizeof(gsinterned_string))
#define GS_NEXT_BYTECODE(p, n) ((struct gsbc *)((uchar*)p + GS_SIZE_BYTECODE(n)))

void *gsreservebytecode(ulong);

void *gsreserveerrors(ulong);

struct gserror *gserror(struct gspos, char *, ...);
struct gserror *gserror_unimpl(char *, int, struct gspos, char *, ...);

void gspoison(struct gsheap_item *, struct gspos, char *, ...);
void gspoison_unimpl(struct gsheap_item *, char *, int, struct gspos, char *, ...);

void *gsreserverecords(ulong);

void *gsreserveeprims(ulong);
