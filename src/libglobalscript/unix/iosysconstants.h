enum gsbio_iochannel_type {
    gsbio_ioread,
    gsbio_iowrite,
    gsbio_iorddir,
    gsbio_iostat,
    gsbio_iogetenv,
};

char * gsbio_efmt_iochannel_type(char *, char *, enum gsbio_iochannel_type);

struct uxio_dir_ichannel {
    struct uxio_ichannel *udir, *p9dir;
};
