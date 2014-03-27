#define BIG_ENDIAN_32(pb) \
    (((u32int)(pb)[0] << 24) | ((u32int)(pb)[1] << 16) | ((u32int)(pb)[2] << 8) | ((u32int)(pb)[3]))

gsinterned_string gsglobal_gslib_dir(void);
void gsadd_global_gslib(gsinterned_string, struct gsfile_symtable **);
void gsadddir(char *, struct gsfile_symtable **);

enum {
    gsstring_code_hash_comments = 1,
    gsstring_code_hash_escapes = 2,
    gsstring_code_hash_is_normal = 4,
};

typedef enum {
    gsfileerror = -1,
    gsfileprefix = 0,
    gsfiledocument = 1,
    gsfileunknown = 0x40,
} gsfiletype;

gsfiletype gsaddfile(char *, struct gsfile_symtable **, struct gspos *, gsvalue *, struct gstype **);

/* Deprecated */
int gssymeq(gsinterned_string, gssymboltype, char*);

/* Caching version */
#define gssymceq(sa, se, st, sn) (se ? (sa) == se : (sa) == (se = gsintern_string((st), (sn))))

struct gsparsedfile_segment {
    struct gsparsedfile_segment *next;
    void *extent;
};

typedef struct gsparsedfile {
    ulong size;
    gsinterned_string name, relname;
    gsfiletype type;
    uint features;
    struct gsdatasection *data;
    struct gscodesection *code;
    struct gstypesection *types;
    struct gscoercionsection *coercions;
    struct gsparsedfile_segment *last_seg;
    struct gsparsedfile_segment first_seg;
} gsparsedfile;

struct gsdatasection {
    struct gsparsedfile_segment *first_seg;
    ulong numitems;
};

struct gscodesection {
    ulong numitems;
};

struct gstypesection {
    struct gsparsedfile_segment *first_seg;
    ulong numitems;
};

struct gscoercionsection {
    struct gsparsedfile_segment *first_seg;
    ulong numitems;
};

#define GSDATA_SECTION_FIRST_ITEM(p) ((struct gsparsedline*)((uchar*)p+sizeof(*p)))
#define GSTYPE_SECTION_FIRST_ITEM(p) ((struct gsparsedline*)((uchar*)p+sizeof(*p)))
#define GSCOERCION_SECTION_FIRST_ITEM(p) ((struct gsparsedline*)((uchar*)p+sizeof(*p)))

struct gsparsedline *gstype_section_next_item(struct gsparsedfile_segment **, struct gsparsedline *);
struct gsparsedline *gscoercion_section_next_item(struct gsparsedfile_segment **, struct gsparsedline *);

struct gsparsedline {
    struct gspos pos;
    ulong numarguments;
    gsinterned_string label;
    gsinterned_string directive;
    gsinterned_string arguments[];
};

struct gsparsedline *gsinput_next_line(struct gsparsedfile_segment **, struct gsparsedline *);

struct gsbc_item {
    gsparsedfile *file;
    gssymboltype type;
    struct gsparsedfile_segment *pseg;
    struct gsparsedline *v;
};

void gsbc_item_empty(struct gsbc_item *);
int gsbc_item_eq(struct gsbc_item, struct gsbc_item);

struct gsbc_code_item_type;

void gsfatal_bad_input(struct gsparsedline *, char *, ...);
void gsargcheck(struct gsparsedline *, ulong, char *, ...);

#define MAX_ITEMS_PER_SCC 0x100

struct gsfile_symtable;

struct gsfile_symtable *gscreatesymtable(struct gsfile_symtable *prev_symtable);

void gssymtable_add_data_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *pdata);
void gssymtable_add_code_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *pcode);
void gssymtable_add_type_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *ptype);
void gssymtable_add_coercion_item(struct gsfile_symtable *symtable, gsinterned_string label, gsparsedfile *file, struct gsparsedfile_segment *pseg, struct gsparsedline *ptype);

void gsappend_symtable(struct gsfile_symtable *symtable0, struct gsfile_symtable *symtable1);

struct gstype;
struct gskind;
struct gsbco;
struct gsbc_coercion_type;

void gssymtable_set_type(struct gsfile_symtable *, gsinterned_string, struct gstype *);
void gssymtable_set_abstype(struct gsfile_symtable *, gsinterned_string, struct gstype *);
void gssymtable_set_type_expr_kind(struct gsfile_symtable *, gsinterned_string, struct gskind *);
void gssymtable_set_data(struct gsfile_symtable *, gsinterned_string, gsvalue);
void gssymtable_set_code(struct gsfile_symtable *, gsinterned_string, struct gsbco *);
void gssymtable_set_data_type(struct gsfile_symtable *symtable, gsinterned_string label, struct gstype *);
void gssymtable_set_code_type(struct gsfile_symtable *, gsinterned_string, struct gsbc_code_item_type *);
void gssymtable_set_coercion_type(struct gsfile_symtable *, gsinterned_string, struct gsbc_coercion_type *);

struct gstype *gssymtable_get_data_type(struct gsfile_symtable *symtable, gsinterned_string label);
struct gskind *gssymtable_get_type_expr_kind(struct gsfile_symtable *, gsinterned_string);
struct gstype *gssymtable_get_type(struct gsfile_symtable *, gsinterned_string);
struct gstype *gssymtable_get_abstype(struct gsfile_symtable *, gsinterned_string);
struct gsbc_kind *gssymtable_get_kind(struct gsfile_symtable *, gsinterned_string);
gsvalue gssymtable_get_data(struct gsfile_symtable *, gsinterned_string);
struct gsbco *gssymtable_get_code(struct gsfile_symtable *, gsinterned_string);
struct gsbc_code_item_type *gssymtable_get_code_type(struct gsfile_symtable *, gsinterned_string);
struct gsbc_coercion_type *gssymtable_get_coercion_type(struct gsfile_symtable *, gsinterned_string);

struct gsbc_item gssymtable_lookup(struct gspos, struct gsfile_symtable *symtable, gsinterned_string label);

struct gsbc_scc *gssymtable_get_scc(struct gsfile_symtable *symtable, struct gsbc_item);
void gssymtable_set_scc(struct gsfile_symtable *symtable, struct gsbc_item, struct gsbc_scc *);
