/* §section{(Byte-Code) Code Segment} */

struct gsbco {
    enum {
        gsbc_expr,
    } tag;
    ulong size;
    ulong numglobals;
};

enum {
    gsbc_op_enter,
};

void *gsreservebytecode(ulong);

void *gsreserveerrors(ulong);
