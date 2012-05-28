#define BIG_ENDIAN_32(pb) \
    (((u32int)(pb)[0] << 24) | ((u32int)(pb)[1] << 16) | ((u32int)(pb)[2] << 8) | ((u32int)(pb)[3]))

int gsisdir(char *filename);
void gsadddir(char *filename);

typedef enum {
    gsfileerror = -1,
    gsfileprefix = 0,
    gsfiledocument = 1,
    gsfileunknown = 0x40,
} gsfiletype;

gsfiletype gsloadfile(char *filename, gsvalue *pentry);

typedef enum {
    gssymfilename,
    gssymdatadirective,
    gssymdatalable,
    gssymcodedirective,
    gssymcodeop,
    gssymcodelable,
    gssymtypelable,
    gssymreglable,
} gssymboltype;

typedef struct gsstring_value {
    ulong size;
    gssymboltype type;
    char name[];
} *gsinterned_string;

gsinterned_string gsintern_string(gssymboltype, char*);

struct gsparsedfile_segment {
    struct gsparsedfile_segment *next;
};

typedef struct gsparsedfile {
    ulong size;
    void *extent;
    gsinterned_string name, relname;
    gsfiletype type;
    struct gsdatasection *data;
    struct gscodesection *code;
    struct gsparsedfile_segment first_seg;
} gsparsedfile;

struct gsdatasection {
    ulong numitems;
};

struct gscodesection {
    ulong numitems;
};

#define GSDATA_SECTION_FIRST_ITEM(p) ((struct gsparsedline*)((uchar*)p+sizeof(*p)))

struct gsparsedline {
    gsinterned_string file;
    uint lineno;
    ulong numarguments;
    gsinterned_string label;
    gsinterned_string directive;
    gsinterned_string arguments[];
};
