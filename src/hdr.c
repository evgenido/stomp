/*
 * Copyright 2013 Evgeni Dobrev <evgeni_dobrev@developer.bg>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <string.h>
#include "hdr.h"

const char *hdr_get(size_t count, const struct stomp_hdr *hdrs, const char *key)
{
	size_t i;
	const struct stomp_hdr *h;
	for (i=0; i < count; i++) {
		h = &hdrs[i];
		if (!strcmp(key, h->key)) {
			return h->val;
		}
	}

	return NULL;
}
