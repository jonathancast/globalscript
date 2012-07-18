enum ibio_iochannel_type {
    ibio_ioread,
    ibio_iowrite,
    ibio_iorddir,
    ibio_iostat,
    ibio_iogetenv,
};

char * ibio_efmt_iochannel_type(char *, char *, enum ibio_iochannel_type);

struct uxio_dir_ichannel {
    struct uxio_ichannel *udir, *p9dir;
};
