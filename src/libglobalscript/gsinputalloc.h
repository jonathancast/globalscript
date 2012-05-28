gsparsedfile *gsparsed_file_alloc(char *filename, char *relname, gsfiletype type);

void *gsparsed_file_extend(gsparsedfile *, ulong, struct gsparsedfile_segment **);
