struct uxio_ichannel *ibio_sys_stat(char *filename);

long gsbio_sys_read_stat(struct uxio_dir_ichannel *);

struct ibio_dir *ibio_parse_stat(struct uxio_ichannel *);
struct ibio_dir *gsbio_sys_parse_stat(struct uxio_dir_ichannel *);
