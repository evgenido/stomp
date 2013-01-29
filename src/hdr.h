#ifndef HDR_H
#define HDR_H

#include "stomp.h"

const char *hdr_get(int count, const stomp_hdr_t *hdrs, const char *key);


#endif /* HDR_H */
