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
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>

enum read_state {
	RS_INIT,
	RS_CMD,
	RS_HDR,
	RS_HDR_ESC,
	RS_BODY,
	RS_DONE,
	RS_ERR
};

struct frame_hdr {
	ptrdiff_t key_offset;
	size_t key_len;
	ptrdiff_t val_offset;
	size_t val_len;
};

struct _frame {
	void *buf;
	size_t buf_len; /* length of data in buf in bytes */
	size_t buf_capacity; /* allocated size in bytes */

	ptrdiff_t cmd_offset; /* offset in buff to the start of the cmd string */
	size_t cmd_len; /* lenght of cmd string in bytes */

	struct frame_hdr *hdrs; /* array of struct frame_hdr elements */
	size_t hdrs_len; /* number of elements in the array */
	size_t hdrs_capacity; /* allocated number of struct frame_hdr elements */

	struct stomp_hdr *stomp_hdrs; /* array of struct stomp_hdr elements */
	size_t stomp_hdrs_len; /* number of elements in the array */
	size_t stomp_hdrs_capacity; /* allocated number of struct stomp_hdr elements */

	ptrdiff_t body_offset; /* offset in buff to the start of the body */
	size_t body_len; /* length of body in bytes */

	enum read_state read_state; /* current state of the frame reading state mashine */
	ptrdiff_t tmp_offset; /* current position within buf while reading an incomming frame */
	size_t tmp_len; /* amount of bytes read while reading an incomming frame */
};
typedef struct _frame frame_t;

frame_t *frame_new();
void frame_free(frame_t *f);
void frame_reset(frame_t *f);
int frame_cmd_set(frame_t *f, const char *cmd);
int frame_hdr_add(frame_t *f, const char *key, const char *val);
int frame_hdrs_add(frame_t *f, size_t hdrc, const struct stomp_hdr *hdrs);
int frame_body_set(frame_t *f, const void *body, size_t len);
ssize_t frame_write(int fd, frame_t *f);

size_t frame_cmd_get(frame_t *f, const char **cmd);
size_t frame_hdrs_get(frame_t *f, const struct stomp_hdr **hdrs);
size_t frame_body_get(frame_t *f, const void **body);
int frame_read(int fd, frame_t *f);

#endif /* FRAME_H */
