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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "frame.h"
#include "hdr.h"

typedef enum {
	RS_INIT,
	RS_CMD,
	RS_HDR,
	RS_HDR_ESC,
	RS_BODY,
	RS_DONE,
	RS_ERR
} read_state_t;

typedef struct {
	size_t key_offset;
	size_t key_len;
	size_t val_offset;
	size_t val_len;
} frame_hdr_t;

struct _frame {
	void *buf;
	size_t buf_len; /* length of data in buf in bytes */
	size_t buf_capacity; /* allocated size in bytes */

	size_t cmd_offset; /* pointer to the start of the cmd string */
	size_t cmd_len; /* lenght of cmd string in bytes */

	frame_hdr_t *hdrs;
	int hdrs_len; 
	int hdrs_capacity; 

	stomp_hdr_t *stomp_hdrs;
	int stomp_hdrs_len; 
	int stomp_hdrs_capacity; 

	size_t body_offset;
	size_t body_len; /* length of body in bytes */

	read_state_t read_state; /* current state of the frame reading state mashine */
	size_t tmp_offset; /* current position within buf while reading an incomming frame */
	size_t tmp_len; /* amount of bytes read while reading an incomming frame */
};

/* number of bytes to increase session->buf by
 * when adding more data to the frame */
#define BUFINCLEN 512

/* number of stomp_hdr_t structures to add to session->hdrs 
 * when adding more data to the frame */
#define HDRINCLEN 4

static int parse_content_length(const char *s, size_t *len)
{
	size_t tmp_len;
	char *endptr;
	const char *nptr = s;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	tmp_len = strtoul(nptr, &endptr, 10);
	if ((errno == ERANGE ) || (errno != 0 && tmp_len == 0)) {
		errno = EINVAL;
		return -1;
	}

	if (endptr == nptr) {
		errno = EINVAL;
		return -1;
	}

	*len = tmp_len;

	return 0;
}


frame_t *frame_new()
{
	frame_t *f = calloc(1, sizeof(*f));
	
	return f;
}

void frame_free(frame_t *f)
{
	free(f->stomp_hdrs);
	free(f->hdrs);
	free(f->buf);
	free(f);
}

void frame_reset(frame_t *f)
{
	void *buf = f->buf;
	size_t capacity = f->buf_capacity;
	frame_hdr_t *hdrs = f->hdrs;
	stomp_hdr_t *stomp_hdrs = f->stomp_hdrs;
	size_t hdrs_capacity = f->hdrs_capacity;
	size_t stomp_hdrs_capacity = f->stomp_hdrs_capacity;
	memset(hdrs, 0, sizeof(*hdrs)*hdrs_capacity);
	memset(f, 0, sizeof(*f));
	f->buf = buf;
	f->buf_capacity = capacity;
	f->hdrs = hdrs;
	f->hdrs_capacity = hdrs_capacity;
	f->stomp_hdrs = stomp_hdrs;
	f->stomp_hdrs_capacity = stomp_hdrs_capacity;
	f->read_state = RS_INIT;
}

static size_t buflene(const void *data, size_t len)
{
	char c;
	size_t lene = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		c = *(char*)(data + i);
		if (c == '\r' || c == '\n' || 
		    c == ':' || c == '\\' ) {
			lene += 2;
		} else {
			lene += 1;
		}
	}

	return lene;
}

static void *frame_alloc(frame_t *f, size_t len)
{
	size_t capacity;
	void *buf;

	if (f->buf_capacity - f->buf_len >= len) {
		return f->buf + f->buf_len;
	}

	capacity = f->buf_capacity + BUFINCLEN;
	buf = realloc(f->buf, capacity);
	if (!buf) {
		return NULL;
	}

	memset(buf + f->buf_len, 0, (capacity - f->buf_len));

	f->buf = buf;
	f->buf_capacity = capacity;

	return f->buf + f->buf_len;
}

static void *frame_bufcat(frame_t *f, const void *data, size_t len)
{
	void *dest;

	dest = frame_alloc(f, len);
	if (!dest) {
		return NULL;
	}
	
	dest = memcpy(dest, data, len);
	
	f->buf_len += len;

	return dest;
}

static void *frame_bufcate(frame_t *f, const void *data, size_t len)
{
	size_t i;
	void *dest;
	char c;
	char *buf;
	size_t buf_len;
	size_t lene;

	lene = buflene(data, len);
	if (lene == len) {
		return frame_bufcat(f, data, len);
	}

	dest = frame_alloc(f, lene);
	if (!dest) {
		return NULL;
	}
	
	for (i = 0; i < len; i++) {
		c = *(char *)(data + i);
		switch(c){
			case '\r':
				buf = "\\r";
				buf_len = 2;
				break;
			case '\n':
				buf = "\\n";
				buf_len = 2;
				break;
			case ':':
				buf = "\\c";
				buf_len = 2;
				break;
			case '\\':
				buf = "\\\\";
				buf_len = 2;
				break;
			default:
				buf = (char *)(data + i);
				buf_len = 1;
		}

		memcpy(f->buf + f->buf_len, buf, buf_len);
		f->buf_len += buf_len;
	}

	return dest;
}

int frame_cmd_set(frame_t *f, const char *cmd)
{
	void *dest;
	size_t len = strlen(cmd);
	
	dest = frame_bufcat(f, cmd, len);
	if (!dest) {
		return -1;
	}
	
	f->cmd_offset = dest - f->buf;
	f->cmd_len = len;

	if (!frame_bufcat(f, "\n", 1)) {
		return -1;
	}

	return 0;
}

int frame_hdr_add(frame_t *f, const char *key, const char *val)
{
	frame_hdr_t *h;
	void *dest;
	size_t len;

	if (!f->cmd_len) {
		errno = EINVAL;
		return -1;
	}
	
	if (f->body_offset) {
		errno = EINVAL;
		return -1;
	}

	if (!(f->hdrs_capacity - f->hdrs_len)) {
		size_t capacity = f->hdrs_capacity + HDRINCLEN;
		h = realloc(f->hdrs, capacity * sizeof(*h));
		if (!h) {
			return -1;
		}
		
		memset(&h[f->hdrs_len], 0, sizeof(*h)*(capacity - f->hdrs_len));

		f->hdrs = h;
		f->hdrs_capacity = capacity;
	}

	h = &f->hdrs[f->hdrs_len];
	len = strlen(key);
	dest = frame_bufcate(f, key, len);
	if (!dest) {
		return -1;
	}

	h->key_offset = dest - f->buf; 

	if (!frame_bufcat(f, ":", 1)) {
		return -1;
	}

	len = strlen(val);
	dest = frame_bufcate(f, val, len);
	if (!dest) {
		return -1;
	}
	
	h->val_offset = dest - f->buf;

	if (!frame_bufcat(f, "\n", 1)) {
		return -1;
	}

	f->hdrs_len += 1;

	return 0;
}

int frame_hdrs_add(frame_t *f, int hdrc, const stomp_hdr_t *hdrs)
{
	int i;
	const stomp_hdr_t *h;

	for (i=0; i < hdrc; i++) {
		h = &hdrs[i];
		if (frame_hdr_add(f, h->key, h->val)) {
			return -1;
		}
	}

	return 0;
}

int frame_body_set(frame_t *f, const void *data, size_t len)
{
	void *dest;
	size_t offset;

	if (!f->cmd_len) {
		return -1;
	}

	if (f->body_offset) {
		return -1;
	}
	
	/* end of headers */ 
	if (!frame_bufcat(f, "\n", 1)) {
		return -1;
	}

	dest = frame_bufcat(f, data, len);
	if (!dest) {
		return -1;
	}

	offset = dest - f->buf;

	/* end of frame */
	if (!frame_bufcat(f, "\0", 1)) {
		return -1;
	}

	f->body_offset = offset;
	f->body_len = len;

	return 0;
}

static read_state_t frame_read_body(frame_t *f, char c) 
{
	void *tmp;
	read_state_t state = f->read_state;
	size_t body_len;
	const char *l;

	tmp = frame_bufcat(f, &c, 1);
	if (!tmp) {
		return RS_ERR;
	}

	if (!f->tmp_offset) {
		f->tmp_offset = tmp - f->buf;
	}

	f->tmp_len += 1;
	
	if (c != '\0') {
		return state;
	}
       	
	/* \0 and no content-type -> frame is over */ 
	/* OR err parsing content-type -> frame is over */ 
	/* OR loaded all data -> frame is over */
	if (!frame_hdr_get(f, "content-length", &l) || parse_content_length(l, &body_len) || f->tmp_len >= body_len){
		f->body_offset = f->tmp_offset;
		f->body_len = f->tmp_len - 1; /* skip last '\0' */
		return RS_DONE;
	}

	return state;
} 

int frame_write(int fd, frame_t *f) 
{
	size_t left; 
	ssize_t n;
	size_t total = 0;

	/* close the frame */
	if (!f->body_offset) {
		if (!frame_bufcat(f, "\n\0", 2)) {
			return -1;
		}
	}

	left = f->buf_len; 
	while(total < f->buf_len) {
		n = write(fd, f->buf+total, left);
		if (n == -1) {
			return -1;
		}

		total += n;
		left -= n;
	}

	return total; 
}

static read_state_t frame_read_init(frame_t *f, char c) 
{
	void *tmp;
	read_state_t state = f->read_state;

	switch (c) {
		case 'C': /* CONNECTED */
		case 'E': /* ERROR */
		case 'R': /* RECEIPT */
		case 'M': /* MESSAGE */
			state = RS_ERR;
			tmp = frame_bufcat(f, &c, 1);
		       	if (tmp) {
				state = RS_CMD;
				f->tmp_offset = tmp - f->buf;
				f->tmp_len = 1;
			}
			break;
		case '\n': /* heart-beat */
			state = RS_DONE;
			break;
		default:
			;
	}

	return state;
} 

static read_state_t frame_read_cmd(frame_t *f, char c) 
{
	read_state_t state = f->read_state;
	
	switch (c) {
		case '\r': 
			break;
		case '\0':
			state = RS_ERR;
			break;
		case '\n':
			state = RS_ERR;
			if (frame_bufcat(f, "\0", 1)) {
				if (!strncmp(f->buf + f->tmp_offset, "CONNECTED", f->tmp_len) || 
					!strncmp(f->buf + f->tmp_offset, "ERROR", f->tmp_len) || 
					!strncmp(f->buf + f->tmp_offset, "RECEIPT", f->tmp_len) || 
					!strncmp(f->buf + f->tmp_offset, "MESSAGE", f->tmp_len)) 
				{
					f->cmd_offset = f->tmp_offset;
					f->cmd_len = f->tmp_len;
					state = RS_HDR;
					f->tmp_offset = 0;
					f->tmp_len = 0;
				} 
			} 
			break;
		default:
			if (!frame_bufcat(f, &c, 1)) {
				state = RS_ERR;
			} else {
				f->tmp_len += 1;
			}
	}

	return state;
} 

static read_state_t frame_read_hdr(frame_t *f, char c) 
{
	frame_hdr_t *h;
	void *tmp;
	size_t count = f->hdrs_len;
	read_state_t state = f->read_state;
	
	if (!(f->hdrs_capacity - count)) {
		size_t capacity = f->hdrs_capacity + HDRINCLEN;
		h = realloc(f->hdrs, capacity * sizeof(*h));
		if (!h) {
			return RS_ERR;
		}

		memset(&h[count], 0, sizeof(*h)*(capacity - count));

		f->hdrs = h;
		f->hdrs_capacity = capacity;
	}

	h = &f->hdrs[f->hdrs_len];
	
	switch (c) {
		case '\0':
			state = RS_ERR;
			break;
		case '\r': 
			break;
		case ':':
			if (!frame_bufcat(f, "\0", 1)) {
				state = RS_ERR;
			} else {
				h->key_offset = f->tmp_offset;
				h->key_len = f->tmp_len;
				f->tmp_offset = 0;
				f->tmp_len = 0;
			}
			break;
		case '\n':
			if (h->key_len) {
				if (!frame_bufcat(f, "\0", 1)) {
					state = RS_ERR;
				} else {
					h->val_offset = f->tmp_offset;
					h->val_len = f->tmp_len;
					f->hdrs_len += 1;
					f->tmp_offset = 0;
					f->tmp_len = 0;
				}
			} else {
				state = RS_BODY;
			} 
			break;
		case '\\':
				state = RS_HDR_ESC;
			break;
		default:
			tmp = frame_bufcat(f, &c, 1);
			if (!tmp) {
				state = RS_ERR;
			} else {
				if (!f->tmp_offset) {
					f->tmp_offset = tmp - f->buf;
				} 
				f->tmp_len += 1;
			}
	}

	return state;
} 

static read_state_t frame_read_hdr_esc(frame_t *f, char c) 
{
	char *buf;
	void *tmp;
	
	if (c == 'r') {
		buf = "\r";
	} else if (c == 'n') {
		buf = "\n";
	} else if (c == 'c') {
		buf = ":";
	} else if (c == '\\') {
		buf = "\\";
	} else {
		return RS_ERR;
	}

	tmp = frame_bufcat(f, buf, 1);
	if (!tmp) {
		return RS_ERR;
	} 

	if (!f->tmp_offset) {
		f->tmp_offset = tmp - f->buf;
	} 

	f->tmp_len += 1;

	return RS_HDR;
} 

int frame_read(int fd, frame_t *f)
{
	char c = 0;
	
	while (f->read_state != RS_ERR && f->read_state != RS_DONE) {

		if(read(fd, &c, sizeof(char)) != sizeof(char)) {
			return -1;
		}

		switch(f->read_state) {
			case RS_INIT:
				f->read_state = frame_read_init(f, c);
				break;
			case RS_CMD:
				f->read_state = frame_read_cmd(f, c);
				break;
			case RS_HDR:
				f->read_state = frame_read_hdr(f, c);
				break;
			case RS_HDR_ESC:
				f->read_state = frame_read_hdr_esc(f, c);
				break;
			case RS_BODY:
				f->read_state = frame_read_body(f, c);
				break;
			default:
				return -1;
		}
	}
	
	if (f->read_state == RS_ERR) {
		return -1;
	}

	return 0;
}

int frame_cmd_get(frame_t *f, const char **cmd)
{
	if (!f->cmd_len) {
		return 0;
	}

	*cmd = f->buf + f->cmd_offset;
	return f->cmd_len;
}

int frame_hdr_get(frame_t *f, const char *key, const char **val)
{
	int i;
	const frame_hdr_t *h;
	for (i=0; i < f->hdrs_len; i++) {
		h = &f->hdrs[i];
		if (!strncmp(key, f->buf + h->key_offset, h->key_len)) {
			*val = f->buf + h->val_offset;
			return h->val_len; 
		}
	}

	return 0;
}

int frame_hdrs_get(frame_t *f, const stomp_hdr_t **hdrs)
{
	stomp_hdr_t *h;
	int i;

	if (!f->hdrs) {
		return 0;
	}

	if (f->stomp_hdrs_len == f->hdrs_len) {
		*hdrs = f->stomp_hdrs;
		return f->stomp_hdrs_len;
	}

	if (f->hdrs_len > f->stomp_hdrs_capacity) {
		h = realloc(f->stomp_hdrs, f->hdrs_len * sizeof(*h));
		if (!h) {
			return -1;
		}
		
		f->stomp_hdrs = h;
		f->stomp_hdrs_capacity = f->hdrs_len;
	}

	for (i=0; i < f->hdrs_len; i++) {
		h = &f->stomp_hdrs[i];
		h->key = f->buf + f->hdrs[i].key_offset;
		h->val = f->buf + f->hdrs[i].val_offset;
	}

	f->stomp_hdrs_len = f->hdrs_len;

	*hdrs = f->stomp_hdrs;
	return f->stomp_hdrs_len;
}

int frame_body_get(frame_t *f, const void **body)
{
	if (!f->body_len) {
		return 0;
	}

	*body = f->buf + f->body_offset;
	return f->body_len;
}

