enum ibio_iochannel_type {
    ibio_ioread,
    ibio_iowrite,
    ibio_iorddir,
    ibio_iostat,
};

void ibio_efmt_iochannel_type(char *, char *, enum ibio_iochannel_type);
