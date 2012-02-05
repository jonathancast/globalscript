
#define UXIO_IO_BUFFER_SIZE (1<<16)
#define UXIO_BEGINNING_OF_IO_BUFFER_AREA(p) ((void*)((uintptr)p & (UXIO_IO_BUFFER_SIZE - 1)))
#define UXIO_END_OF_IO_BUFFER(p) ((void*)((uchar*)UXIO_BEGINNING_OF_IO_BUFFER_AREA(p) + UXIO_IO_BUFFER_SIZE))

struct uxio_channel {
    int fd;
    enum ibio_iochannel_type ty;
    void *free_beg;
    void *free_end;
};

struct uxio_channel *ibio_get_channel_for_external_io(int fd, enum ibio_iochannel_type ty);
