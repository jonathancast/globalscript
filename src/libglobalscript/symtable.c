/* §source.file{Symbol Tables} */

#include <u.h>
#include <libc.h>
#include <libglobalscript.h>
#include "gsinputfile.h"

/* §section Symbol Equality */

int
gssymeq(gsinterned_string sy0, gssymboltype ty, char *str)
{
    gsinterned_string sy1;

    sy1 = gsintern_string(ty, str);

    return sy0 == sy1;
}

/* §section Interning Symbols */

static gsinterned_string gsget_string(gssymboltype ty, char *nm);
static gsinterned_string gsalloc_string(gssymboltype ty, char *nm);
static void gsstore_string(gssymboltype ty, char *nm, gsinterned_string addr);

gsinterned_string
gsintern_string(gssymboltype ty, char *nm)
{
    gsinterned_string res;

    if (res = gsget_string(ty, nm))
        return res;

    res = gsalloc_string(ty, nm);
    gsstore_string(ty, nm, res);

    return res;
}

/* §subsection Internals */

static ulong gshash_string();

struct gstring_hash_link {
    gsinterned_string string;
    struct gstring_hash_link *next;
};

static ulong string_num_buckets, string_hash_size;
static struct gstring_hash_link **string_intern_hash;

static void gsstring_initialize_intern_hash(void);
static void gsstring_expand_hash_table(void);

/* §subsection{Retrieving Interned Strings} */

static
gsinterned_string
gsget_string(gssymboltype ty, char *nm)
{
    ulong hash;
    ulong n;
    struct gstring_hash_link *p;

    hash = gshash_string(ty, nm);

    if (!string_num_buckets)
        gsstring_initialize_intern_hash();

    n = hash % string_num_buckets;
    for (p = string_intern_hash[n]; p; p = p->next) {
        if (
            p->string->type == ty
            && !strncmp(p->string->name, nm, ((char*)p->string + p->string->size) - p->string->name)
        )
            return p->string;
    }

    return 0;
}

/* §subsection{Retrieving Interned Strings} */

static struct gstring_hash_link *gsstring_alloc_hash_link(void);

static
void
gsstore_string(gssymboltype ty, char *nm, gsinterned_string addr)
{
    ulong hash, n;
    struct gstring_hash_link *p;

    if (!string_num_buckets)
        gsstring_initialize_intern_hash();

    string_hash_size++;
    if (string_hash_size > string_num_buckets >> 1)
        gsstring_expand_hash_table();

    hash = gshash_string(ty, nm);

    n = hash % string_num_buckets;
    p = gsstring_alloc_hash_link();
    p->string = addr;
    p->next = string_intern_hash[n];

    string_intern_hash[n] = p;
}

/* §subsection{Common Functions} */

static
ulong
gshash_string(gssymboltype ty, char *nm)
{
    ulong hash = 0;
    char *p;

    hash = ty;
    p = nm;
    while (*p)
        hash += *p++;

    return hash;
}

/* §subsection{Dynamic Allocation for the Interning Hash Table} */

struct gs_block_class gsstringhash_desc = {
    /* evaulator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Interning Hash",
};

void *gsstringhash_nursury;

static
void
gsstring_initialize_intern_hash(void)
{
    struct gstring_hash_link **p;

    string_num_buckets = 0x40;
    string_intern_hash = gs_sys_seg_suballoc(
        &gsstringhash_desc,
        &gsstringhash_nursury,
        string_num_buckets * sizeof(*string_intern_hash),
        sizeof(*string_intern_hash)
    );

    for (p = string_intern_hash; p < string_intern_hash + string_num_buckets; p++)
        *p = 0
    ;
}

static
void
gsstring_expand_hash_table()
{
    ulong newnumbuckets;
    struct gstring_hash_link **new_hash, **p, *p1, *p2;
    ulong hash, n;

    newnumbuckets = 2*string_num_buckets;

    if (newnumbuckets * sizeof(*new_hash) > BLOCK_SIZE)
        gsfatal("%s:%d: Out of memory for intern hash", __FILE__, __LINE__)
    ;

    new_hash = gs_sys_seg_suballoc(
        &gsstringhash_desc,
        &gsstringhash_nursury,
        newnumbuckets * sizeof(*string_intern_hash),
        sizeof(*string_intern_hash)
    );

    for (p = new_hash; p < new_hash + newnumbuckets; p++)
        *p = 0
    ;

    for (p = string_intern_hash; p < string_intern_hash + string_num_buckets; p++) {
        for (p1 = *p; p1; p1 = p1->next) {
            hash = gshash_string(p1->string->type, p1->string->name);
            n = hash % newnumbuckets;
            p2 = gsstring_alloc_hash_link();
            p2->string = p1->string;
            p2->next = new_hash[n];
            new_hash[n] = p2;
        }
    }

    string_intern_hash = new_hash;
    string_num_buckets = newnumbuckets;
}

/* §subsection{Dynamic Allocation for Interning Hash Table Alist Entries */

void *hash_link_nursury;

static void gsalloc_new_hash_link_block(void);

struct hash_link_block {
    struct gs_blockdesc hdr;
};

static
struct gstring_hash_link *
gsstring_alloc_hash_link()
{
    struct hash_link_block *hash_link_nursury_seg;
    struct gstring_hash_link *res;

    if (!hash_link_nursury)
        gsalloc_new_hash_link_block();

    hash_link_nursury_seg = BLOCK_CONTAINING(hash_link_nursury);
    if ((uchar*)END_OF_BLOCK(hash_link_nursury_seg) - (uchar*)hash_link_nursury < sizeof(struct gstring_hash_link)) {
        gsalloc_new_hash_link_block();
        hash_link_nursury_seg = BLOCK_CONTAINING(hash_link_nursury);
    }
    res = hash_link_nursury;

    hash_link_nursury = res + 1;
    if ((uintptr)hash_link_nursury % sizeof(void*))
        hash_link_nursury = (uchar*)hash_link_nursury + sizeof(void*) - ((uintptr)hash_link_nursury % sizeof(void*));

    if ((uchar*)hash_link_nursury >= (uchar*)END_OF_BLOCK(hash_link_nursury_seg))
        hash_link_nursury = 0;

    return res;
}

struct gs_block_class gshash_link_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Hash alist link for interned symbol hash",
};

static
void
gsalloc_new_hash_link_block()
{
    struct hash_link_block *nursury_seg;

    nursury_seg = gs_sys_seg_alloc(&gshash_link_desc);
    hash_link_nursury = (uchar*)nursury_seg + sizeof(*nursury_seg);
    gsassert(__FILE__, __LINE__, !((uintptr)hash_link_nursury % sizeof(void*)), "hash_link_nursury not void*-aligned; check sizeof(struct string_block)");
}

/* §subsection{Dynamic Allocation for Strings} */

static void *string_nursury;

static void gsalloc_new_string_block(void);

struct string_block {
    struct gs_blockdesc hdr;
};

gsinterned_string
gsalloc_string(gssymboltype ty, char *nm)
{
    struct string_block *string_nursury_seg;
    gsinterned_string res;
    ulong size;

    if (!string_nursury)
        gsalloc_new_string_block()
    ;

    size = sizeof(*res) + strlen(nm) + 1;
    if (size % sizeof(ulong))
        size += sizeof(ulong) - (size % sizeof(ulong))
    ;

    string_nursury_seg = BLOCK_CONTAINING(string_nursury);
    if ((uchar*)END_OF_BLOCK(string_nursury_seg) - (uchar*)string_nursury < size)
        gsalloc_new_string_block()
    ;

    res = string_nursury;
    res->size = size;
    res->type = ty;
    strecpy(res->name, (char*)res + size, nm);
    string_nursury = (uchar*)res + size;

    return res;
}

struct gs_block_class gsstring_desc = {
    /* evaluator = */ gsnoeval,
    /* indirection_dereferencer = */ gsnoindir,
    /* description = */ "Interned strings",
};

static
void
gsalloc_new_string_block()
{
    struct string_block *nursury_seg;

    nursury_seg = gs_sys_seg_alloc(&gsstring_desc);
    string_nursury = (uchar*)nursury_seg + sizeof(*nursury_seg);
    gsassert(__FILE__, __LINE__, !((uintptr)string_nursury % sizeof(ulong)), "string_nursury not ulong-aligned; check sizeof(struct string_block)");
}

