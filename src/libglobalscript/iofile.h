#define UXIO_IO_BUFFER_SIZE (1<<16)
#define UXIO_BEGINNING_OF_IO_BUFFER_AREA(p) ((void*)((uintptr)p & ~(UXIO_IO_BUFFER_SIZE - 1)))
#define UXIO_END_OF_IO_BUFFER(p) ((void*)((uchar*)UXIO_BEGINNING_OF_IO_BUFFER_AREA(p) + UXIO_IO_BUFFER_SIZE))

struct uxio_ichannel {
    int fd;
    char *filename;
    enum gsbio_iochannel_type ty;
    int at_eof;
    void *buf_beg;
    void *data_beg;
    void *data_end;
    vlong offset;
};

struct gs_sys_global_block_suballoc_info uxio_filename_info;

struct uxio_ichannel *gsbio_get_channel_for_external_io(char *filename, int fd, enum gsbio_iochannel_type ty);

ulong uxio_channel_size_of_available_data(struct uxio_ichannel *);

void *uxio_save_space(struct uxio_ichannel *, ulong);
long uxio_consume_space(struct uxio_ichannel *, void **, ulong);
