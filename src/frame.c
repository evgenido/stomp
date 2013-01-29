#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "frame.h"
#include "hdr.h"

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
	free(f->hdrs);
	free(f->buf);
	free(f);
}

void frame_reset(frame_t *f)
{
	void *buf = f->buf;
	size_t capacity = f->buf_capacity;
	stomp_hdr_t *hdrs = f->hdrs;
	size_t hdrs_capacity = f->hdrs_capacity;
	memset(hdrs, 0, sizeof(*hdrs)*hdrs_capacity);
	memset(f, 0, sizeof(*f));
	f->buf = buf;
	f->buf_capacity = capacity;
	f->hdrs = hdrs;
	f->hdrs_capacity = hdrs_capacity;
	f->read_state = RS_INIT;
}

size_t buflene(const void *data, size_t len)
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

void *frame_alloc(frame_t *f, size_t len)
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

void *frame_bufcat(frame_t *f, const void *data, size_t len)
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

void *frame_bufcate(frame_t *f, const void *data, size_t len)
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
	size_t len = strlen(cmd);
	
	f->cmd = frame_bufcat(f, cmd, len);
	if (!f->cmd) {
		return -1;
	}
	
	f->cmd_len = len;

	if (!frame_bufcat(f, "\n", 1)) {
		return -1;
	}

	return 0;
}

int frame_hdr_add(frame_t *f, const char *key, const char *val)
{
	stomp_hdr_t *h;
	size_t len;

	if (!f->cmd) {
		errno = EINVAL;
		return -1;
	}
	
	if (f->body) {
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
	h->key = frame_bufcate(f, key, len);
	if (!h->key) {
		return -1;
	}

	if (!frame_bufcat(f, ":", 1)) {
		return -1;
	}

	len = strlen(val);
	h->val = frame_bufcate(f, val, len);
	if (!h->val) {
		return -1;
	}

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

	if (!f->cmd) {
		return -1;
	}

	if (f->body) {
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

	/* end of frame */
	if (!frame_bufcat(f, "\0", 1)) {
		return -1;
	}

	f->body = dest;
	f->body_len = len;

	return 0;
}

static read_state_t frame_read_body(frame_t *f, char c) 
{
	void *tmp;
	read_state_t state = f->read_state;
	size_t body_len;
	const char *l = hdr_get(f->hdrs_len, f->hdrs, "content-length");

	tmp = frame_bufcat(f, &c, 1);
	if (!tmp) {
		return RS_ERR;
	}

	if (!f->tmp) {
		f->tmp = tmp;
	}

	f->tmp_len += 1;
	
	if (c != '\0') {
		return state;
	}

	/* \0 and no content-type -> frame is over */ 
	/* OR err parsing content-type -> frame is over */ 
	/* OR loaded all data -> frame is over */
	if (!l || parse_content_length(l, &body_len) || f->tmp_len >= body_len){
		f->body = f->tmp;
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
	if (!f->body) {
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
				f->tmp = tmp;
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
				if (!strncmp(f->tmp, "CONNECTED", f->tmp_len) || 
					!strncmp(f->tmp, "ERROR", f->tmp_len) || 
					!strncmp(f->tmp, "RECEIPT", f->tmp_len) || 
					!strncmp(f->tmp, "MESSAGE", f->tmp_len)) 
				{
					f->cmd = f->tmp;
					f->cmd_len = f->tmp_len;
					state = RS_HDR;
					f->tmp = NULL;
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
	stomp_hdr_t *h;
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
				h->key = f->tmp;
				f->tmp = NULL;
				f->tmp_len = 0;
			}
			break;
		case '\n':
			if (h->key) {
				if (!frame_bufcat(f, "\0", 1)) {
					state = RS_ERR;
				} else {
					h->val = f->tmp;
					f->hdrs_len += 1;
					f->tmp = NULL;
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
				if (!f->tmp) {
					f->tmp = tmp;
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

	if (!f->tmp) {
		f->tmp = tmp;
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
