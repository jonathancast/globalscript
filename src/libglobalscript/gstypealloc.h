struct gstype {
    enum {
        gstype_uninitialized,
        gstype_prim,
        gstype_abstract,
        gstype_expr,
    } node;
    gsinterned_string file;
    uint lineno;
    union {
        struct {
            struct gsregistered_primset *primset;
            gsinterned_string name;
            struct gskind *kind;
        } prim;
        struct gstype_expr_summary *expr;
    } a;
};

struct gstype_expr_summary {
    int numregs, numglobals, numcodelabels, numfvs, numargs, numforalls, numlets;
    struct gstype **globals;
    gsinterned_string *global_vars;
    struct gstype **code_label_dests;
    gsinterned_string *code_labels;
    struct gskind **fvkinds, **argkinds, **forallkinds;
    struct gstype_expr_let *lets;
    struct gstype_expr *code;
};

struct gstype_expr_let {
    int body;
    int numfvs;
    int *fvs;
};

struct gstype_expr {
    enum {
        gstype_lift,
        gstype_app,
        gstype_ref,
        gstype_sum,
    } node;
    gsinterned_string file;
    uint lineno;
};

struct gstype_expr_lift {
    struct gstype_expr e;
    struct gstype_expr *arg;
};

struct gstype_expr_app {
    struct gstype_expr e;
    struct gstype_expr *fun;
    int numargs;
    int args[];
};

struct gstype_expr_ref {
    struct gstype_expr e;
    int referent;
};

struct gstype_constr {
    gsinterned_string name;
    int arg;
};

struct gstype_expr_sum {
    struct gstype_expr e;
    int numconstrs;
    struct gstype_constr constrs[];
};

void gsfatal_unimpl_type(char *, int, struct gstype *, char *, ...);

void gstypes_alloc_for_scc(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int);
void gstypes_compile_types(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int);

struct gstype_item_kind {
    int numfvs;
    struct gskind **fvkinds;
    struct gskind *kind;
};

struct gskind {
    enum {
        gskind_unknown,
        gskind_unlifted,
        gskind_lifted,
        gskind_exponential,
    } node;
    struct gskind *args[];
};

struct gskind *gskind_compile(struct gsparsedline *, gsinterned_string);
struct gskind *gstypes_compile_prim_kind(struct gsregistered_primkind *);

struct gskind *gskind_unknown_kind(void);
struct gskind *gskind_unlifted_kind(void);
struct gskind *gskind_lifted_kind(void);
struct gskind *gskind_exponential_kind(struct gskind *, struct gskind *);
