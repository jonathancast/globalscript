
#define UXIO_IO_BUFFER_SIZE (1<<16)

struct uxio_channel {
    int fd;
    enum ibio_iochannel_type ty;
    void *free_beg;
    void *free_end;
};

struct uxio_channel *ibio_get_channel_for_external_io(int fd, enum ibio_iochannel_type ty);
