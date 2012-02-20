#define GET_LITTLE_ENDIAN_U16INT(p) ((u16int)*(uchar*)(p) + ((u16int)*((uchar*)(p)+1)<<8))
#define PUT_LITTLE_ENDIAN_U16INT(p, n) \
    do { \
        *(uchar*)(p) = ((u16int)(n)) & 0xff; \
        *((uchar*)(p)+1) = (((u16int)(n)) & 0xff00) >> 8; \
    } while (0)

#define GET_LITTLE_ENDIAN_U32INT(p) (\
    (u32int)*(uchar*)(p) \
    + ((u32int)*((uchar*)(p)+1)<<8) \
    + ((u32int)*((uchar*)(p)+2)<<16) \
    + ((u32int)*((uchar*)(p)+3)<<24) \
    )
#define PUT_LITTLE_ENDIAN_U32INT(p, n) \
    do { \
        *(uchar*)(p) = ((u32int)(n)) & 0xff; \
        *((uchar*)(p) + 1) = (((u32int)(n)) & 0xff00) >> 8; \
        *((uchar*)(p) + 2) = (((u32int)(n)) & 0xff0000) >> 16; \
        *((uchar*)(p) + 3) = (((u32int)(n)) & 0xff000000) >> 24; \
    } while (0)
