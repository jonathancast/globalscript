/* §section (Byte-Code) Code Segment */

struct gsbc {
    struct gspos pos;
    uchar instr;
    uchar args[];
};

enum {
    gsbc_op_efv,
    gsbc_op_closure,
    gsbc_op_unknown_prim,
    gsbc_op_prim,
    gsbc_op_constr,
    gsbc_op_record,
    gsbc_op_field,
    gsbc_op_lfield,
    gsbc_op_undefined,
    gsbc_op_alias,
    gsbc_op_apply,
    gsbc_op_unknown_api_prim,
    gsbc_op_api_prim,
    gsbc_op_app,
    gsbc_op_force,
    gsbc_op_strict,
    gsbc_op_ubanalzye,
    gsbc_op_analyze,
    gsbc_op_danalyze,
    gsbc_op_undef,
    gsbc_op_enter,
    gsbc_op_yield,
    gsbc_op_ubprim,
    gsbc_op_unknown_ubprim,
    gsbc_op_lprim,
    gsbc_op_unknown_lprim,
    gsbc_op_bind_closure,
    gsbc_op_bind_apply,
    gsbc_op_body_closure,
    gsbc_op_body_undefined,
    gsbc_op_body_alias,
};

#define GS_NTH_ARG_OFFSET(n) (offsetof(struct gsbc, args) + n)

#define GS_SIZE_BYTECODE(n) (GS_NTH_ARG_OFFSET(n) + sizeof(gsinterned_string) - GS_NTH_ARG_OFFSET(n) % sizeof(gsinterned_string))
#define GS_NEXT_BYTECODE(p, n) ((struct gsbc *)((uchar*)p + GS_SIZE_BYTECODE(n)))

#define ACE_EFV_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_EFV_REGNUM(ip) ((ip)->args[0])
#define ACE_EFV_SKIP(ip) GS_NEXT_BYTECODE(ip, 1)

#define ACE_CLOSURE_SIZE(nfvs) GS_SIZE_BYTECODE(2 + (nfvs))
#define ACE_CLOSURE_CODE(ip) ((ip)->args[0])
#define ACE_CLOSURE_NUMFVS(ip) ((ip)->args[1])
#define ACE_CLOSURE_FV(ip, n) ((ip)->args[2+(n)])
#define ACE_CLOSURE_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_CLOSURE_NUMFVS(ip))

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

#define ACE_SIZE_RECORD(nfields) GS_SIZE_BYTECODE(1 + nfields)
#define ACE_RECORD_NUMFIELDS(ip) ((ip)->args[0])
#define ACE_RECORD_FIELD(ip, n) ((ip)->args[1 + (n)])
#define ACE_RECORD_SKIP(ip) GS_NEXT_BYTECODE(ip, 1 + ACE_RECORD_NUMFIELDS(ip))

#define ACE_FIELD_SIZE() GS_SIZE_BYTECODE(2)
#define ACE_FIELD_FIELD(ip) ((ip)->args[0])
#define ACE_FIELD_RECORD(ip) ((ip)->args[1])
#define ACE_FIELD_SKIP(ip) GS_NEXT_BYTECODE((ip), 2)

#define ACE_LFIELD_SIZE() GS_SIZE_BYTECODE(2)
#define ACE_LFIELD_FIELD(ip) ((ip)->args[0])
#define ACE_LFIELD_RECORD(ip) ((ip)->args[1])
#define ACE_LFIELD_SKIP(ip) GS_NEXT_BYTECODE((ip), 2)

#define ACE_UNDEFINED_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_UNDEFINED_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_ALIAS_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_ALIAS_SOURCE(ip) ((ip)->args[0])
#define ACE_ALIAS_SKIP(ip) GS_NEXT_BYTECODE(ip, 1)

#define ACE_APPLY_SIZE(nargs) GS_SIZE_BYTECODE(2 + (nargs))
#define ACE_APPLY_FUN(ip) ((ip)->args[0])
#define ACE_APPLY_NUM_ARGS(ip) ((ip)->args[1])
#define ACE_APPLY_ARG(ip, n) ((ip)->args[2 + (n)])
#define ACE_APPLY_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_APPLY_NUM_ARGS(ip))

#define ACE_IMPPRIM_SIZE(nargs) GS_SIZE_BYTECODE(2 + (nargs))
#define ACE_IMPPRIM_INDEX(ip) ((ip)->args[0])
#define ACE_IMPPRIM_NUMARGS(ip) ((ip)->args[1])
#define ACE_IMPPRIM_ARG(ip, n) ((ip)->args[2 + (n)])
#define ACE_IMPPRIM_SKIP(ip) GS_NEXT_BYTECODE(ip, 2 + ACE_IMPPRIM_NUMARGS(ip))

#define ACE_APP_SIZE(nargs) GS_SIZE_BYTECODE(1 + nargs)
#define ACE_APP_NUMARGS(ip) ((ip)->args[0])
#define ACE_APP_ARG(ip, n) ((ip)->args[1 + (n)])
#define ACE_APP_SKIP(ip) GS_NEXT_BYTECODE((ip), 1 + ACE_APP_NUMARGS(ip))

#define ACE_FORCE_SIZE(nfvs) GS_SIZE_BYTECODE(2 + nfvs)
#define ACE_FORCE_CONT(ip) ((ip)->args[0])
#define ACE_FORCE_NUMFVS(ip) ((ip)->args[1])
#define ACE_FORCE_FV(ip, n) ((ip)->args[2 + (n)])
#define ACE_FORCE_SKIP(ip) GS_NEXT_BYTECODE(ip, 2 + ACE_FORCE_NUMFVS(ip))

#define ACE_STRICT_SIZE(nfvs) GS_SIZE_BYTECODE(2 + nfvs)
#define ACE_STRICT_CONT(ip) ((ip)->args[0])
#define ACE_STRICT_NUMFVS(ip) ((ip)->args[1])
#define ACE_STRICT_FV(ip, n) ((ip)->args[2 + (n)])
#define ACE_STRICT_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_STRICT_NUMFVS(ip))

#define ACE_UBANALYZE_SIZE(ncases, nfvs) GS_SIZE_BYTECODE(2 + ncases + nfvs)
#define ACE_UBANALYZE_NUMCONTS(ip) ((ip)->args[0])
#define ACE_UBANALYZE_CONT(ip, n) ((ip)->args[2 + (n)])
#define ACE_UBANALYZE_NUMFVS(ip) ((ip)->args[1])
#define ACE_UBANALYZE_FV(ip, n) ((ip)->args[2 + ACE_UBANALYZE_NUMCONTS(ip) + (n)])
#define ACE_UBANALYZE_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_UBANALYZE_NUMCONTS(ip) + ACE_UBANALYZE_NUMFVS(ip))

#define ACE_UBPRIM_SIZE(nargs) GS_SIZE_BYTECODE(3 + nargs)
#define ACE_UBPRIM_PRIMSET_INDEX(ip) ((ip)->args[0])
#define ACE_UBPRIM_INDEX(ip) ((ip)->args[1])
#define ACE_UBPRIM_NARGS(ip) ((ip)->args[2])
#define ACE_UBPRIM_ARG(ip, n) ((ip)->args[3 + (n)])
#define ACE_UBPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 3 + ACE_UBPRIM_NARGS(ip))

#define ACE_UNKNOWN_UBPRIM_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_UNKNOWN_UBPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_LPRIM_SIZE(nargs) GS_SIZE_BYTECODE(3 + nargs)
#define ACE_LPRIM_PRIMSET_INDEX(ip) ((ip)->args[0])
#define ACE_LPRIM_INDEX(ip) ((ip)->args[1])
#define ACE_LPRIM_NARGS(ip) ((ip)->args[2])
#define ACE_LPRIM_ARG(ip, n) ((ip)->args[3 + (n)])
#define ACE_LPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 3 + ACE_UBPRIM_NARGS(ip))

#define ACE_UNKNOWN_LPRIM_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_UNKNOWN_LPRIM_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_ANALYZE_SCRUTINEE(ip) ((ip)->args[0])
#define ACE_ANALYZE_CASES(ip) ((struct gsbc **)GS_NEXT_BYTECODE(ip, 1))

#define ACE_DANALYZE_SIZE(nconstrs) (GS_SIZE_BYTECODE(2 + (nconstrs)) + (1 + (nconstrs)) * sizeof(struct gsbc *))
#define ACE_DANALYZE_SCRUTINEE(ip) ((ip)->args[0])
#define ACE_DANALYZE_NUMCONSTRS(ip) ((ip)->args[1])
#define ACE_DANALYZE_CONSTR(ip, n) ((ip)->args[2 + (n)])
#define ACE_DANALYZE_CASES(ip) ((struct gsbc **)GS_NEXT_BYTECODE(ip, 2 + ACE_DANALYZE_NUMCONSTRS(ip)))

#define ACE_UNDEF_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_UNDEF_SKIP(ip) GS_NEXT_BYTECODE(ip, 0)

#define ACE_ENTER_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_ENTER_ARG(ip) ((ip)->args[0])
#define ACE_ENTER_SKIP(ip) GS_NEXT_BYTECODE(ip, 1)

#define ACE_YIELD_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_YIELD_ARG(ip) ((ip)->args[0])
#define ACE_YIELD_SKIP(ip) GS_NEXT_BYTECODE(ip, 1)

#define ACE_BIND_CLOSURE_SIZE(nfvs) GS_SIZE_BYTECODE(2 + nfvs)
#define ACE_BIND_CLOSURE_CODE(ip) ((ip)->args[0])
#define ACE_BIND_CLOSURE_NUMFVS(ip) ((ip)->args[1])
#define ACE_BIND_CLOSURE_FV(ip, i) ((ip)->args[2+(i)])
#define ACE_BIND_CLOSURE_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_BIND_CLOSURE_NUMFVS(ip))

#define ACE_BIND_APPLY_SIZE(nargs) GS_SIZE_BYTECODE(2 + (nargs))
#define ACE_BIND_APPLY_FUN(ip) ((ip)->args[0])
#define ACE_BIND_APPLY_NUM_ARGS(ip) ((ip)->args[1])
#define ACE_BIND_APPLY_ARG(ip, n) ((ip)->args[2 + (n)])
#define ACE_BIND_APPLY_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_APPLY_NUM_ARGS(ip))

#define ACE_BODY_CLOSURE_SIZE(nfvs) GS_SIZE_BYTECODE(2 + nfvs)
#define ACE_BODY_CLOSURE_CODE(ip) ((ip)->args[0])
#define ACE_BODY_CLOSURE_NUMFVS(ip) ((ip)->args[1])
#define ACE_BODY_CLOSURE_FV(ip, i) ((ip)->args[2+(i)])
#define ACE_BODY_CLOSURE_SKIP(ip) GS_NEXT_BYTECODE((ip), 2 + ACE_BODY_CLOSURE_NUMFVS(ip))

#define ACE_BODY_UNDEFINED_SIZE() GS_SIZE_BYTECODE(0)
#define ACE_BODY_UNDEFINED_SKIP(ip) GS_NEXT_BYTECODE((ip), 0)

#define ACE_BODY_ALIAS_SIZE() GS_SIZE_BYTECODE(1)
#define ACE_BODY_ALIAS_SOURCE(ip) ((ip)->args[0])
#define ACE_BODY_ALIAS_SKIP(ip) GS_NEXT_BYTECODE((ip), 1)

void *gsreservebytecode(ulong);

