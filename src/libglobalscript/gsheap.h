/* §section (Byte-Code) Code Segment */

struct gsbc {
    struct gspos pos;
    uchar instr;
    uchar args[];
};

enum {
    gsbc_op_efv,
    gsbc_op_alloc,
    gsbc_op_unknown_prim,
    gsbc_op_prim,
    gsbc_op_constr,
    gsbc_op_record,
    gsbc_op_field,
    gsbc_op_unknown_eprim,
    gsbc_op_eprim,
    gsbc_op_app,
    gsbc_op_force,
    gsbc_op_ubanalzye,
    gsbc_op_analyze,
    gsbc_op_undef,
    gsbc_op_enter,
    gsbc_op_yield,
    gsbc_op_ubprim,
    gsbc_op_unknown_ubprim,
    gsbc_op_bind,
    gsbc_op_body,
};

#define GS_NTH_ARG_OFFSET(n) (offsetof(struct gsbc, args) + n)

#define GS_SIZE_BYTECODE(n) (GS_NTH_ARG_OFFSET(n) + sizeof(gsinterned_string) - GS_NTH_ARG_OFFSET(n) % sizeof(gsinterned_string))
#define GS_NEXT_BYTECODE(p, n) ((struct gsbc *)((uchar*)p + GS_SIZE_BYTECODE(n)))

#define ACE_EFV_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_EFV_REGNUM(ip) ((ip)->args[0])
#define ACE_EFV_SKIP(ip) GS_NEXT_BYTECODE(ip, 1)

#define ACE_ALLOC_CODE(ip) ((ip)->args[0])
#define ACE_ALLOC_NUMFVS(ip) ((ip)->args[1])
#define ACE_ALLOC_FV(ip, n) ((ip)->args[2+(n)])
#define ACE_ALLOC_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_ALLOC_NUMFVS(ip))

#define ACE_UNKNOWN_PRIM_SIZE() (0)
#define ACE_UNKNOWN_PRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_PRIM_SIZE(nargs) GS_SIZE_BYTECODE(3 + (nargs))
#define ACE_PRIM_PRIMSET_INDEX(ip) ((ip)->args[0])
#define ACE_PRIM_INDEX(ip) ((ip)->args[1])
#define ACE_PRIM_NARGS(ip) ((ip)->args[2])
#define ACE_PRIM_ARG(ip, n) ((ip)->args[3 + (n)])
#define ACE_PRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 3 + ACE_PRIM_NARGS(ip))

#define ACE_CONSTR_CONSTRNUM(ip) ((ip)->args[0])
#define ACE_CONSTR_NUMARGS(ip) ((ip)->args[1])
#define ACE_CONSTR_ARG(ip, n) ((ip)->args[2 + (n)])
#define ACE_CONSTR_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_CONSTR_NUMARGS(ip))

#define ACE_RECORD_FIELD(ip, n) ((ip)->args[1 + (n)])

#define ACE_FIELD_SIZE() GS_SIZE_BYTECODE(2)
#define ACE_FIELD_FIELD(ip) ((ip)->args[0])
#define ACE_FIELD_RECORD(ip) ((ip)->args[1])
#define ACE_FIELD_SKIP(ip) GS_NEXT_BYTECODE((ip), 2)

#define ACE_UBANALYZE_SIZE(ncases, nfvs) GS_SIZE_BYTECODE(2 + ncases + nfvs)
#define ACE_UBANALYZE_NUMCONTS(ip) ((ip)->args[0])
#define ACE_UBANALYZE_CONT(ip, n) ((ip)->args[2 + (n)])
#define ACE_UBANALYZE_NUMFVS(ip) ((ip)->args[1])
#define ACE_UBANALYZE_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_UBANALYZE_NUMCONTS(ip) + ACE_UBANALYZE_NUMFVS(ip))

#define ACE_UBANALYZE_STACK_SIZE(nconts, nfvs) (sizeof(struct gsbc_cont_ubanalyze) + nconts * sizeof(struct gsbco *) + nfvs * sizeof(gsvalue))

#define ACE_UBPRIM_SIZE(nargs) GS_SIZE_BYTECODE(2 + nargs)
#define ACE_UBPRIM_PRIMSET_INDEX(ip) ((ip)->args[0])
#define ACE_UBPRIM_INDEX(ip) ((ip)->args[1])
#define ACE_UBPRIM_NARGS(ip) ((ip)->args[2])
#define ACE_UBPRIM_ARG(ip, n) ((ip)->args[3 + (n)])
#define ACE_UBPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_UBPRIM_NARGS(ip))

#define ACE_UNKNOWN_UBPRIM_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_UNKNOWN_UBPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_ANALYZE_SCRUTINEE(ip) ((ip)->args[0])
#define ACE_ANALYZE_CASES(ip) ((struct gsbc **)GS_NEXT_BYTECODE(ip, 1))

#define ACE_BIND_CODE(ip) ((ip)->args[0])
#define ACE_BIND_NUMFVS(ip) ((ip)->args[1])
#define ACE_BIND_FV(ip, i) ((ip)->args[2+(i)])
#define ACE_BIND_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_BIND_NUMFVS(ip))

void *gsreservebytecode(ulong);

/* §section Continuations */

struct gsbc_cont {
    enum {
        gsbc_cont_app,
        gsbc_cont_force,
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

struct gsbc_cont_ubanalyze {
    struct gsbc_cont cont;
    int numconts;
    struct gsbco **conts;
    int numfvs;
    gsvalue *fvs;
};

/* §section Global Script Run-time Errors */

void *gsreserveerrors(ulong);

struct gserror *gserror(struct gspos, char *, ...);

void gspoison(struct gsheap_item *, struct gspos, char *, ...);
void gspoison_unimpl(struct gsheap_item *, char *, int, struct gspos, char *, ...);

/* §section Global Script Implementation Errors */

void *gsreserveimplementation_failures(ulong);

struct gsimplementation_failure *gsunimpl(char *, int, struct gspos, char *, ...);

/* §section Records */

void *gsreserverecords(ulong);

/* §section Constructors */

void *gsreserveconstrs(ulong);

/* §section API Primitives */

void *gsreserveeprims(ulong);
