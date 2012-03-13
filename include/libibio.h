#ifndef _IBIO_H
#define _IBIO_H 1
#if defined(__cplusplus)
extern "C" {
#endif   

struct ibio_dir {
    long size;
    Dir d;
};

struct ibio_dir *ibio_stat(char *filename);

struct uxio_ichannel *ibio_device_iopen(char *filename, int omode);

#if defined(__cplusplus)
}
#endif
#endif	/* _IBIO_H */
