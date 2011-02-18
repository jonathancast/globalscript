#ifndef _GSINPUTFILE_H_
#define _GSINPUTFILE_H_ 1

#define BIG_ENDIAN_32(pb) \
    (((pb)[0] << 24) | ((pb)[1] << 16) | ((pb)[2] << 8) | ((pb)[3]))

typedef enum {
    gsfileerror = -1,
    gsfileprefix = 0,
    gsfiledocument = 1,
    gsfileunknown = 0x40,
} gsfiletype;

struct gsheader
{
    gsfiletype ty;
    struct {
        uchar era, major, minor, step;
    } version;
    long strings_length, code_length, data_length;
    union {
        long s_entry_point;
        gsvalue entry_point;
    };
};
typedef struct gsheader gsheader;

gsfiletype gsloadfile(char *filename, gsheader *phdr);

#endif
