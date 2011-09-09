#ifndef _GSINPUTFILE_H_
#define _GSINPUTFILE_H_ 1

#define BIG_ENDIAN_32(pb) \
    (((u32int)(pb)[0] << 24) | ((u32int)(pb)[1] << 16) | ((u32int)(pb)[2] << 8) | ((u32int)(pb)[3]))

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
};
typedef struct gsheader gsheader;

gsfiletype gsloadfile(char *filename, gsheader *phdr);

#endif
