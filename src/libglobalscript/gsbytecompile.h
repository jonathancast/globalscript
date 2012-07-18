struct gsbco;

void gsbc_alloc_data_for_scc(struct gsfile_symtable *, struct gsbc_item *, gsvalue *, int);
void gsbc_alloc_code_for_scc(struct gsfile_symtable *, struct gsbc_item *, struct gsbco **, int);
void gsbc_bytecompile_scc(struct gsfile_symtable *, struct gsbc_item *, gsvalue *, struct gsbco **, int);
