struct uxio_ichannel *gsbio_sys_stat(char *filename);

long gsbio_sys_read_stat(struct uxio_dir_ichannel *);

struct gsbio_dir *gsbio_parse_stat(struct uxio_ichannel *);
struct gsbio_dir *gsbio_sys_parse_stat(struct uxio_dir_ichannel *);
