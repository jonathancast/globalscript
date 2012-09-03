struct gstype {
    enum {
        gstype_uninitialized = -1,
        gstype_indirection,
        gstype_abstract,
        gstype_prim,
        gstype_var,
        gstype_lambda,
        gstype_forall,
        gstype_lift,
        gstype_app,
        gstype_ref,
        gstype_sum,
    } node;
    gsinterned_string file;
    uint lineno;
};

struct gstype_indirection {
    struct gstype e;
    struct gstype *referent;
};

struct gstype_abstract {
    struct gstype e;
    gsinterned_string name;
    struct gskind *kind;
};

struct gstype_prim {
    struct gstype e;
    struct gsregistered_primset *primset;
    gsinterned_string name;
    struct gskind *kind;
};

struct gstype_var {
    struct gstype e;
    gsinterned_string name;
    struct gskind *kind;
};

struct gstype_lambda {
    struct gstype e;
    gsinterned_string var;
    struct gskind *kind;
    struct gstype *body;
};

struct gstype_forall {
    struct gstype e;
    gsinterned_string var;
    struct gskind *kind;
    struct gstype *body;
};

struct gstype_lift {
    struct gstype e;
    struct gstype *arg;
};

struct gstype_app {
    struct gstype e;
    struct gstype *fun;
    struct gstype *arg;
};

struct gstype_ref {
    struct gstype e;
    int referent;
};

struct gstype_constr {
    gsinterned_string name;
    int arg;
};

struct gstype_sum {
    struct gstype e;
    int numconstrs;
    struct gstype_constr constrs[];
};

void gsfatal_unimpl_type(char *, int, struct gstype *, char *, ...);

void gstypes_alloc_for_scc(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gstype **, int);
void gstypes_compile_types(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gstype **, int);

struct gskind {
    enum {
        gskind_unknown,
        gskind_unlifted,
        gskind_lifted,
        gskind_exponential,
    } node;
    /* NB: First argument (on exponentials) is the §emph{base} (result) second argument is the §emph{exponent} (argument) */
    struct gskind *args[];
};

struct gskind *gskind_compile(struct gsparsedline *, gsinterned_string);
struct gskind *gstypes_compile_prim_kind(struct gsregistered_primkind *);

struct gskind *gskind_unknown_kind(void);
struct gskind *gskind_unlifted_kind(void);
struct gskind *gskind_lifted_kind(void);
    /* NB: First argument (on exponentials) is the §emph{base} (result) second argument is the §emph{exponent} (argument) */
struct gskind *gskind_exponential_kind(struct gskind *, struct gskind *);
