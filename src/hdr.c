#include <stddef.h>
#include <string.h>
#include "hdr.h"

const char *hdr_get(int count, const stomp_hdr_t *hdrs, const char *key)
{
	int i;
	const stomp_hdr_t *h;
	for (i=0; i < count; i++) {
		h = &hdrs[i];
		if (!strcmp(key, h->key)) {
			return h->val;
		}
	}

	return NULL;
}
