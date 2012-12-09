void gstypes_process_type_declarations(struct gsfile_symtable *, struct gsbc_item *, struct gskind **, int);

void gsbc_typecheck_check_boxed(struct gspos, struct gstype *);
void gsbc_typecheck_check_boxed_or_product(struct gspos, struct gstype *);

void gstypes_kind_check_scc(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gstype **, struct gskind **, int);

struct gskind *gstypes_calculate_kind(struct gstype *);

int gstypes_kind_check(struct gspos, struct gskind *, struct gskind *, char *, char *);
void gstypes_kind_check_fail(struct gspos, struct gskind *, struct gskind *);

void gstypes_process_type_signatures(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, int);

void gstypes_type_check_scc(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, struct gstype **, int);
