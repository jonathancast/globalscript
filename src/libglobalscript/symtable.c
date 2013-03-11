/* §source.file Symbol Tables */

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

static ulong gshash_string(gssymboltype, char *);
static gsinterned_string gsget_string(ulong, gssymboltype, char *);
static gsinterned_string gsalloc_string(ulong, gssymboltype, char *);
static void gsstore_string(gsinterned_string);

gsinterned_string
gsintern_string(gssymboltype ty, char *nm)
{
    ulong hash;
    gsinterned_string res;

    hash = gshash_string(ty, nm);

    if (res = gsget_string(hash, ty, nm))
        return res
    ;

    res = gsalloc_string(hash, ty, nm);
    gsstore_string(res);

    return res;
}

/* §subsection Internals */

struct gstring_hash_link {
    gsinterned_string string;
    struct gstring_hash_link *next;
};

static ulong gsstring_num_buckets, gsstring_hash_size;
static struct gstring_hash_link **gsstring_intern_hash;

static void gsstring_initialize_intern_hash(void);
static void gsstring_expand_hash_table(void);

/* §subsection{Retrieving Interned Strings} */

static
gsinterned_string
gsget_string(ulong hash, gssymboltype ty, char *nm)
{
    ulong n;
    struct gstring_hash_link *p;

    if (!gsstring_num_buckets)
        gsstring_initialize_intern_hash()
    ;

    n = hash % gsstring_num_buckets;
    for (p = gsstring_intern_hash[n]; p; p = p->next)
        if (
            p->string->type == ty
            && !strncmp(p->string->name, nm, ((char*)p->string + p->string->size) - p->string->name)
        )
            return p->string
    ;

    return 0;
}

/* §subsection Retrieving Interned Strings */

static struct gstring_hash_link *gsstring_alloc_hash_link(void);

static
void
gsstore_string(gsinterned_string addr)
{
    ulong n;
    struct gstring_hash_link *p;

    if (!gsstring_num_buckets)
        gsstring_initialize_intern_hash()
    ;

    gsstring_hash_size++;
    if (gsstring_hash_size > gsstring_num_buckets >> 1)
        gsstring_expand_hash_table()
    ;

    n = addr->hash % gsstring_num_buckets;
    p = gsstring_alloc_hash_link();
    p->string = addr;
    p->next = gsstring_intern_hash[n];

    gsstring_intern_hash[n] = p;
}

/* §subsection Common Functions */

/* ↓ §ccode{djb2} algorithm from §url{http://www.cse.yorku.ca/~oz/hash.html} */
static
ulong
gshash_string(gssymboltype ty, char *nm)
{
    ulong hash = 5381;
    char *p;

    hash = hash * 33 + ty;
    p = nm;
    while (*p)
        hash = hash * 33 + *p++
    ;

    return hash;
}

/* §subsection Dynamic Allocation for the Interning Hash Table */

struct gs_sys_global_block_suballoc_info gsstringhash_info = {
    /* descr = */ {
        /* evaulator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Interning Hash",
    },
};

static int gsstring_intern_hash_gc_pre_callback_registered;
static gs_sys_gc_pre_callback gsstring_intern_hash_gc_pre_callback;

static
void
gsstring_initialize_intern_hash(void)
{
    struct gstring_hash_link **p;

    gsstring_num_buckets = 0x40;
    gsstring_intern_hash = gs_sys_global_block_suballoc(&gsstringhash_info, gsstring_num_buckets * sizeof(*gsstring_intern_hash));

    for (p = gsstring_intern_hash; p < gsstring_intern_hash + gsstring_num_buckets; p++)
        *p = 0
    ;

    if (!gsstring_intern_hash_gc_pre_callback_registered) {
        gs_sys_gc_pre_callback_register(gsstring_intern_hash_gc_pre_callback);
        gsstring_intern_hash_gc_pre_callback_registered = 1;
    }
}

void
gsstring_intern_hash_gc_pre_callback()
{
    gsstring_num_buckets = 0;
    gsstring_intern_hash = 0;
}

static
void
gsstring_expand_hash_table()
{
    ulong newnumbuckets;
    struct gstring_hash_link **new_hash, **p, *p1, *p2;
    ulong hash, n;

    newnumbuckets = 2*gsstring_num_buckets;

    if (newnumbuckets * sizeof(*new_hash) > BLOCK_SIZE)
        gsfatal("%s:%d: Out of memory for intern hash", __FILE__, __LINE__)
    ;

    new_hash = gs_sys_global_block_suballoc(&gsstringhash_info, newnumbuckets * sizeof(*gsstring_intern_hash));

    for (p = new_hash; p < new_hash + newnumbuckets; p++)
        *p = 0
    ;

    for (p = gsstring_intern_hash; p < gsstring_intern_hash + gsstring_num_buckets; p++) {
        for (p1 = *p; p1; p1 = p1->next) {
            hash = gshash_string(p1->string->type, p1->string->name);
            n = hash % newnumbuckets;
            p2 = gsstring_alloc_hash_link();
            p2->string = p1->string;
            p2->next = new_hash[n];
            new_hash[n] = p2;
        }
    }

    gsstring_intern_hash = new_hash;
    gsstring_num_buckets = newnumbuckets;
}

/* §subsection Dynamic Allocation for Interning Hash Table Alist Entries */


struct gs_sys_global_block_suballoc_info gshash_link_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Hash alist link for interned symbol hash",
    },
};

static
struct gstring_hash_link *
gsstring_alloc_hash_link()
{
    return gs_sys_global_block_suballoc(&gshash_link_info, sizeof(struct gstring_hash_link));
}

/* §subsection Dynamic Allocation for Strings */

struct gs_sys_global_block_suballoc_info gsstring_info = {
    /* descr = */ {
        /* evaluator = */ gsnoeval,
        /* indirection_dereferencer = */ gsnoindir,
        /* gc_trace = */ gsunimplgc,
        /* description = */ "Interned strings",
    },
};

gsinterned_string
gsalloc_string(ulong hash, gssymboltype ty, char *nm)
{
    gsinterned_string res;
    ulong size;

    size = sizeof(*res) + strlen(nm) + 1;
    res = gs_sys_global_block_suballoc(&gsstring_info, size);
    res->size = size;
    res->hash = hash;
    res->type = ty;
    strecpy(res->name, (char*)res + size, nm);

    return res;
}
