#ifndef _IBIO_H
#define _IBIO_H 1
#if defined(__cplusplus)
extern "C" {
#endif   

#include <libc.h>

Dir *ibio_stat(char *filename);

#if defined(__cplusplus)
}
#endif
#endif	/* _IBIO_H */
