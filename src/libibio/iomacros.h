#define GET_LITTLE_ENDIAN_U16INT(p) ((u16int)*(uchar*)(p) + ((u16int)*((uchar*)(p)+1)<<8))
#define PUT_LITTLE_ENDIAN_U16INT(p, n) \
    do { \
        *(uchar*)(p) = ((u16int)n) & 0xff; \
        *((uchar*)(p)+1) = (((u16int)n) & 0xff00) >> 8; \
    } while (0)
