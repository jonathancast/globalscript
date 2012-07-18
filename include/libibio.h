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
struct uxio_dir_ichannel *ibio_dir_iopen(char *filename, int omode);
struct uxio_ichannel *ibio_envvar_iopen(char *name);
long ibio_device_iclose(struct uxio_ichannel *);

long ibio_device_getline(struct uxio_ichannel *chan, char *line, long max);
long ibio_get_contents(struct uxio_ichannel *chan, char *buf, long max);

struct ibio_dir *ibio_read_stat(struct uxio_dir_ichannel *);

int ibio_idevice_at_eof(struct uxio_ichannel *chan);

#if defined(__cplusplus)
}
#endif
#endif	/* _IBIO_H */
