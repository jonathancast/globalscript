struct ibio_channel *ibio_get_channel_for_external_io(enum ibio_iochannel_type ty);
void *ibio_extend_external_io_buffer(struct ibio_channel *, long);
iptr_t ibio_initial_pos(struct ibio_channel *);
