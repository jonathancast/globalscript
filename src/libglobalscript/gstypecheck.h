void gstypes_process_type_declarations(struct gsfile_symtable *, struct gsbc_item *, struct gskind **, int);

void gstypes_kind_check_scc(struct gsfile_symtable *, struct gsbc_item *, struct gstype **, struct gskind **, int);

struct gskind *gstypes_calculate_kind(struct gstype *);

void gstypes_kind_check(gsinterned_string, int, struct gskind *, struct gskind *);
