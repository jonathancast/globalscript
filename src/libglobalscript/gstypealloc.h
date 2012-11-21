struct gsbc_code_item_type {
    enum {
        gsbc_code_item_expr,
        gsbc_code_item_force_cont,
        gsbc_code_item_ubcase_cont,
        gsbc_code_item_eprog,
    } type;
    int numftyvs;
    int numfvs;
    gsinterned_string *tyfvs;
    struct gskind **tyfvkinds;
    struct gstype **fvtypes;
    struct gstype *cont_arg_type;
    struct gstype *result_type;
};

struct gstype {
    enum {
        gstype_abstract,
        gstype_knprim,
        gstype_unprim,
        gstype_var,
        gstype_lambda,
        gstype_forall,
        gstype_lift,
        gstype_app,
        gstype_fun,
        gstype_sum,
        gstype_ubsum,
        gstype_product,
        gstype_ubproduct,
        gstype_coerce_definition,
    } node;
    struct gspos pos;
};

struct gstype_abstract {
    struct gstype e;
    gsinterned_string name;
    struct gskind *kind;
};

struct gstype_knprim {
    struct gstype e;
    enum gsprim_type_group primtypegroup;
    struct gsregistered_primset *primset;
    gsinterned_string primname;
    struct gskind *kind;
};

struct gstype_unprim {
    struct gstype e;
    enum gsprim_type_group primtypegroup;
    gsinterned_string primsetname;
    gsinterned_string primname;
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

struct gstype_fun {
    struct gstype e;
    struct gstype *tyarg;
    struct gstype *tyres;
};

struct gstype_constr {
    gsinterned_string name;
    struct gstype *argtype;
};

struct gstype_sum {
    struct gstype e;
    int numconstrs;
    struct gstype_constr constrs[];
};

struct gstype_ubsum {
    struct gstype e;
    int numconstrs;
    struct gstype_constr constrs[];
};

struct gstype_field {
    gsinterned_string name;
    struct gstype *type;
};

struct gstype_product {
    struct gstype e;
    int numfields;
    struct gstype_field fields[];
};

struct gstype_ubproduct {
    struct gstype e;
    int numfields;
    struct gstype_field fields[];
};

struct gstype_coerce_definition {
    struct gstype e;
    struct gstype *dest, *source;
    int numargs;
};

void gsfatal_bad_type(gsinterned_string, int, struct gstype *, char *, ...);

char *gstypes_eprint_type(char *, char *, struct gstype *);

int gstypes_is_ftyvar(gsinterned_string, struct gstype *);

void gstypes_compile_types(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int);
void gstypes_compile_type_definitions(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int);

struct gsbc_coercion_arg {
    gsinterned_string var;
    struct gskind *kind;
};

struct gsbc_coercion_type {
    struct gstype *source, *dest;
};

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

struct gskind *gskind_compile(struct gspos, gsinterned_string);

struct gskind *gskind_unknown_kind(void);
struct gskind *gskind_unlifted_kind(void);
struct gskind *gskind_lifted_kind(void);
    /* NB: First argument (on exponentials) is the §emph{base} (result) second argument is the §emph{exponent} (argument) */
struct gskind *gskind_exponential_kind(struct gskind *, struct gskind *);
