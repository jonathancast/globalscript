struct gsbc_scc {
    struct gsbc_item item;
    struct gsbc_scc *next_item;
    struct gsbc_scc *next_scc;
};

struct gsbc_scc *gsbc_topsortfile(gsparsedfile *parsedfile, struct gsfile_symtable *symtable);
