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
long ibio_device_iclose(struct uxio_ichannel *);

long ibio_device_getline(struct uxio_ichannel *chan, char *line, long max);

#if defined(__cplusplus)
}
#endif
#endif	/* _IBIO_H */
