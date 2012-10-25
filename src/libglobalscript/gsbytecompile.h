struct gsbc_data_locs {
    gsvalue heap[MAX_ITEMS_PER_SCC];
    gsvalue errors[MAX_ITEMS_PER_SCC];
    gsvalue indir[MAX_ITEMS_PER_SCC];
    gsvalue records[MAX_ITEMS_PER_SCC];
};

void gsbc_alloc_data_for_scc(struct gsfile_symtable *, struct gsbc_item *, struct gsbc_data_locs *, int);
void gsbc_alloc_code_for_scc(struct gsfile_symtable *, struct gsbc_item *, struct gsbco **, int);
void gsbc_bytecompile_scc(struct gsfile_symtable *, struct gsbc_item *, struct gsbc_data_locs *, struct gsbco **, int);
