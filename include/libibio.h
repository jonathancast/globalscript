#ifndef _IBIO_H
#define _IBIO_H 1
#if defined(__cplusplus)
extern "C" {
#endif   

#include <libc.h>

typedef void *iptr_t;


struct ibio_dir *ibio_stat(char *filename);

#if defined(__cplusplus)
}
#endif
#endif	/* _IBIO_H */
