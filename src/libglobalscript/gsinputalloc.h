gsparsedfile *gsparsed_file_alloc(char *filename, char *relname, gsfiletype type);

/* Allocate data from a file; leave file->last_seg pointing at the segment containing the allocated data. */
void *gsparsed_file_extend(gsparsedfile *, ulong);

/* Allocate and return storage for a line; leave file->last_seg pointing at the segment containing that line. */
struct gsparsedline *gsparsed_file_addline(char *, gsparsedfile *, int, ulong);
