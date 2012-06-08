gsparsedfile *gsparsed_file_alloc(char *filename, char *relname, gsfiletype type);

void *gsparsed_file_extend(gsparsedfile *, ulong, struct gsparsedfile_segment **);

struct gsparsedline *gsparsed_file_addline(char *, gsparsedfile *, struct gsparsedfile_segment **, int, ulong);
