/* §section{(Byte-Code) Code Segment} */

struct gsbco {
    enum {
        gsbco_undefined,
    } tag;
    ulong size;
};

void *gsreservebytecode(ulong);

void *gsreserveerrors(ulong);
