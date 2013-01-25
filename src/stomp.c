#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include "stomp.h"

/* enough space for ULLONG_MAX as string */
#define MAXBUFLEN 25

/* number of bytes to increase session->buf by
 * when adding more data to the frame */
#define BUFINCLEN 512

/* number of stomp_hdr_t structures to add to session->hdrs 
 * when adding more data to the frame */
#define HDRINCLEN 4

/* max number of broker heartbeat timeouts */
#define MAXBROKERTMOUTS 5

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
	char *buf;
	size_t buf_len; /* length of data in buf in bytes */
	size_t buf_capacity; /* allocated size in bytes */

	char *cmd; /* pointer to the start of the cmd string */
	size_t cmd_len; /* lenght of cmd string in bytes */

	stomp_hdr_t *hdrs;
	int hdrs_len; 
	int hdrs_capacity; 

	void *body; /* pointer to the message body */
	size_t body_len; /* length of body in bytes */

	read_state_t read_state; /* current state of the frame reading state mashine */
	void *tmp; /* current position within buf while reading an incomming frame */
	size_t tmp_len; /* amount of bytes read while reading an incomming frame */
} frame_t;

typedef enum {
	SPL_10,
	SPL_11,
	SPL_12
} stomp_prot_t;

typedef struct {
	stomp_callback_t connected;
	stomp_callback_t message;
	stomp_callback_t error;
	stomp_callback_t receipt;
	stomp_callback_t user;
} stomp_callbacks_t;

struct _stomp_session {
	stomp_callbacks_t callbacks; /* event callbacks */
	void *ctx; /* pointer to user supplied session context */

	frame_t *frame_out; /* library -> broker */
	frame_t *frame_in; /* broker -> library */

	stomp_prot_t protocol;
	int broker_fd;
	int client_id; /* unique ids for subscribe */
	long client_hb; /* client heart beat period in milliseconds */
	long broker_hb; /* broker heart beat period in milliseconds */
	struct timespec last_write;
	struct timespec last_read;
	int broker_timeouts; 
	int run;
};


static frame_t *frame_new()
{
	frame_t *f = calloc(1, sizeof(*f));
	
	return f;
}

static void frame_free(frame_t *f)
{
	free(f->hdrs);
	free(f->buf);
	free(f);
}

static void frame_reset(frame_t *f)
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

static void *frame_bufcate(frame_t *f, const void *data, size_t len, size_t *lene)
{
	size_t i;
	void *dest;
	char c;
	char *buf;
	size_t buf_len;

	*lene = buflene(data, len);
	if (*lene == len) {
		return frame_bufcat(f, data, len);
	}

	dest = frame_alloc(f, *lene);
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

static int frame_cmd_set(frame_t *f, const char *cmd)
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

static int frame_hdr_add(frame_t *f, const char *key, const char *val)
{
	stomp_hdr_t *h;
	size_t len;
	size_t lene;

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
	h->key = frame_bufcate(f, key, len, &lene);
	if (!h->key) {
		return -1;
	}

	if (!frame_bufcat(f, ":", 1)) {
		return -1;
	}

	len = strlen(val);
	h->val = frame_bufcate(f, val, len, &lene);
	if (!h->val) {
		return -1;
	}

	if (!frame_bufcat(f, "\n", 1)) {
		return -1;
	}

	f->hdrs_len += 1;

	return 0;
}

static int frame_hdrs_add(frame_t *f, int hdrc, const stomp_hdr_t *hdrs)
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

static int frame_body_set(frame_t *f, const void *data, size_t len)
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

static const char *hdr_get(int count, const stomp_hdr_t *hdrs, const char *key)
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

static int frame_write(int fd, frame_t *f) 
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

static int parse_heartbeat(const char *s, long *x, long *y)
{
	long tmp_x, tmp_y;
	char *endptr;
	const char *nptr = s;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	tmp_x = strtol(nptr, &endptr, 10);
	if ((errno == ERANGE && (tmp_x == LONG_MAX || tmp_x == LONG_MIN)) || (errno != 0 && tmp_x == 0)) {
		errno = EINVAL;
		return -1;
	}

	if (tmp_x < 0) {
		errno = EINVAL;
		return -1;
	}

	if (endptr == nptr) {
		errno = EINVAL;
		return -1;
	}

	if (*endptr != ',') {
		errno = EINVAL;
		return -1;
	}
	
	nptr = endptr;
	nptr++;

	errno = 0;
	tmp_y = strtol(nptr, &endptr, 10);
	if ((errno == ERANGE && (tmp_y == LONG_MAX || tmp_y == LONG_MIN)) || (errno != 0 && tmp_y == 0)) {
		errno = EINVAL;
		return -1;
	}
	
	if (tmp_y < 0) {
		errno = EINVAL;
		return -1;
	}

	if (endptr == nptr) {
		errno = EINVAL;
		return -1;
	}
	
	*x = tmp_x;
	*y = tmp_y;

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

static int frame_read(int fd, frame_t *f)
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

stomp_session_t *stomp_session_new(void *session_ctx)
{
	stomp_session_t *s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}

	s->ctx = session_ctx;
	s->broker_fd = -1;

	s->frame_out = frame_new();
	if (!s->frame_out) {
		free(s);
	}

	s->frame_in = frame_new();
	if (!s->frame_in) {
		free(s->frame_out);
		free(s);
	}

	return s;
}

void stomp_session_free(stomp_session_t *s)
{
	frame_free(s->frame_out);
	frame_free(s->frame_in);
	free(s);
}

void stomp_callback_set(stomp_session_t *s, stomp_cb_type_t type, stomp_callback_t cb)
{
	if (!s) {
		return;
	}

	switch (type) {
		case SCB_CONNECTED:
			s->callbacks.connected = cb;
			break;
		case SCB_ERROR:
			s->callbacks.error = cb;
			break;
		case SCB_MESSAGE:
			s->callbacks.message = cb;
			break;
		case SCB_RECEIPT:
			s->callbacks.receipt = cb;
			break;
		case SCB_USER:
			s->callbacks.user = cb;
		default:
			return;
	}
}

void stomp_callback_del(stomp_session_t *s, stomp_cb_type_t type)
{
	if (!s) {
		return;
	}

	switch (type) {
		case SCB_CONNECTED:
			s->callbacks.connected = NULL;
			break;
		case SCB_ERROR:
			s->callbacks.error = NULL;
			break;
		case SCB_MESSAGE:
			s->callbacks.message = NULL;
			break;
		case SCB_RECEIPT:
			s->callbacks.receipt = NULL;
			break;
		case SCB_USER:
			s->callbacks.user = NULL;
		default:
			return;
	}
}


int stomp_connect(stomp_session_t *s, const char *host, const char *service, int hdrc, const stomp_hdr_t *hdrs)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd;
	int err;
	long x = 0;
	long y = 0;
	const char *hb = hdr_get(hdrc, hdrs, "heart-beat");

	if (hb && parse_heartbeat(hb, &x, &y)) {
		errno = EINVAL;
		return -1;
	}
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	err = getaddrinfo(host, service, &hints, &result);
	if (err != 0) {
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break; 

		close(sfd);
	}


	if (rp == NULL) { 
		freeaddrinfo(result);
		return -1;
	}
	
	freeaddrinfo(result);

	s->broker_fd = sfd;
	s->run = 1;
	
	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "CONNECT")) {
		return -1;
	}
	
	s->client_hb = x;
	s->broker_hb = y;
	
	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(sfd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_disconnect(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "DISCONNECT")) {
		return -1;
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);
	
	return 0;
}

// TODO enforce different client-ids in case they are provided with hdrs
int stomp_subscribe(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	const char *ack;
	char buf[MAXBUFLEN];
	int client_id = 0;

	if (!hdr_get(hdrc, hdrs, "destination")) {
		errno = EINVAL;
		return -1;
	}
	
	ack = hdr_get(hdrc, hdrs, "ack");
	if (ack && strcmp(ack, "auto") && strcmp(ack, "client") && strcmp(ack, "client-individual")) {
		errno = EINVAL;
		return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "SUBSCRIBE")) {
		return -1;
	}
	
	if (!hdr_get(hdrc, hdrs, "id")) {
		client_id = s->client_id;
		if (client_id == INT_MAX) {
			client_id = 0;
		}
		client_id++;
		snprintf(buf, MAXBUFLEN, "%d", client_id);
		if (frame_hdr_add(s->frame_out, "id", buf)) {
			return -1;
		}
	} 

	
	if (!ack && frame_hdr_add(s->frame_out, "ack", "auto")) {
		return -1;
	}
	
	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}
	
	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &s->last_write);
	s->client_id = client_id;

	return client_id;
}

int stomp_unsubscribe(stomp_session_t *s, int client_id, int hdrc, const stomp_hdr_t *hdrs)
{
	char buf[MAXBUFLEN];
	const char *id = hdr_get(hdrc, hdrs, "id");
	const char *destination = hdr_get(hdrc, hdrs, "destination");

	if (s->protocol == SPL_10) {
		if (!destination && !id && !client_id) {
			errno = EINVAL;
			return -1;
		}
	} else {
		if (!id && !client_id) {
			errno = EINVAL;
			return -1;
		}
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "UNSUBSCRIBE")) {
		return -1;
	}
	
	// user provided client id. overrride all other supplied headers
	if (client_id) {
		snprintf(buf, MAXBUFLEN, "%lu", (unsigned long)client_id);
		if (frame_hdr_add(s->frame_out, "id", buf)) {
			return -1;
		}
	}
	
	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

// TODO enforce different tx_ids
int stomp_begin(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	if (!hdr_get(hdrc, hdrs, "transaction")) {
		errno = EINVAL;
		return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "BEGIN")) {
		return -1;
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}
	
	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_abort(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	if (!hdr_get(hdrc, hdrs, "transaction")) {
		errno = EINVAL;
		return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "ABORT")) {
		return -1;
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_ack(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	switch(s->protocol) {
		case SPL_12:
			if (!hdr_get(hdrc, hdrs, "id")) {
				errno = EINVAL;
				return -1;
			}
			break;
		case SPL_11:
			if (!hdr_get(hdrc, hdrs, "message-id")) {
				errno = EINVAL;
				return -1;
			}
			if (!hdr_get(hdrc, hdrs, "subscription")) {
				errno = EINVAL;
				return -1;
			}
			break;
		default: /* SPL_10 */
			if (!hdr_get(hdrc, hdrs, "message-id")) {
				errno = EINVAL;
				return -1;
			}
	}

	
	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "ACK")) {
		return -1;
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_nack(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	switch(s->protocol) {
		case SPL_12:
			if (!hdr_get(hdrc, hdrs, "id")) {
				errno = EINVAL;
				return -1;
			}
			break;
		case SPL_11:
			if (!hdr_get(hdrc, hdrs, "message-id")) {
				errno = EINVAL;
				return -1;
			}
			if (!hdr_get(hdrc, hdrs, "subscription")) {
				errno = EINVAL;
				return -1;
			}
			break;
		default: /* SPL_10 */
			errno = EINVAL;
			return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "NACK")) {
		return -1;
	}
	
	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_commit(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs)
{
	if (!hdr_get(hdrc, hdrs, "transaction")) {
		errno = EINVAL;
		return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "COMMIT")) {
		return -1;
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

int stomp_send(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs, void *body, size_t body_len)
{
	char buf[MAXBUFLEN];
	const char *len;

	if (!hdr_get(hdrc, hdrs, "destination")) {
		errno = EINVAL;
		return -1;
	}

	frame_reset(s->frame_out);

	if (frame_cmd_set(s->frame_out, "SEND")) {
		return -1;
	}
	
	// frames SHOULD include a content-length
	len = hdr_get(hdrc, hdrs, "content-length");
	if (!len) {
		snprintf(buf, MAXBUFLEN, "%lu", (unsigned long)body_len);
		if (frame_hdr_add(s->frame_out, "content-length", buf)) {
			return -1;
		}
	}

	if (frame_hdrs_add(s->frame_out, hdrc, hdrs)) {
		return -1;
	}

	if (frame_body_set(s->frame_out, body, body_len)) {
		return -1;
	}
	
	if (frame_write(s->broker_fd, s->frame_out) < 0) {
		s->run = 0;
		return -1;
	}
	
	clock_gettime(CLOCK_MONOTONIC, &s->last_write);

	return 0;
}

static void on_connected(stomp_session_t *s) 
{ 
	stomp_ctx_connected_t e;
	frame_t *f = s->frame_in;
	long x, y;
	const char *v = hdr_get(f->hdrs_len, f->hdrs, "version");
	const char *hb = hdr_get(f->hdrs_len, f->hdrs, "heart-beat");

	if (!strcmp(v, "1.2")) {
		s->protocol = SPL_12;
	} else if (!strcmp(v, "1.1")) {
		s->protocol = SPL_11;
	} else if (!strcmp(v, "1.0")) {
		s->protocol = SPL_10;
	} else {
		s->protocol = SPL_10;
	}

	if (hb && !parse_heartbeat(hb, &x, &y)) {
		if (!s->client_hb || !y) {
			s->client_hb = 0;
		} else {
			s->client_hb = s->client_hb > y ? s->client_hb : y;
		}

		if (!s->broker_hb || !x) {
			s->broker_hb = 0;
		} else {
			s->broker_hb = s->broker_hb > x ? s->broker_hb : x;
		}
	} else {
		s->client_hb = 0;
		s->broker_hb = 0;
	}

	if (!s->callbacks.connected) {
		return;
	}

	e.hdrc = f->hdrs_len;
	e.hdrs = f->hdrs;

	s->callbacks.connected(s, &e, s->ctx);
}

static void on_receipt(stomp_session_t *s) 
{ 
	stomp_ctx_receipt_t e;

	if (!s->callbacks.receipt) {
		return;
	}

	e.hdrc = s->frame_in->hdrs_len;
	e.hdrs = s->frame_in->hdrs;

	s->callbacks.receipt(s, &e, s->ctx);
}

static void on_error(stomp_session_t *s) 
{ 
	stomp_ctx_error_t e;

	if (!s->callbacks.error) {
		return;
	}

	e.hdrc = s->frame_in->hdrs_len;
	e.hdrs = s->frame_in->hdrs;
	e.body_len = s->frame_in->body_len;
	e.body = s->frame_in->body;

	s->callbacks.error(s, &e, s->ctx);
}

static void on_message(stomp_session_t *s) 
{ 
	stomp_ctx_message_t e;

	if (!s->callbacks.message) {
		return;
	}
	
	e.hdrc = s->frame_in->hdrs_len;
	e.hdrs = s->frame_in->hdrs;
	e.body_len = s->frame_in->body_len;
	e.body = s->frame_in->body;

	s->callbacks.message(s, &e, s->ctx);
}

static int on_server_cmd(stomp_session_t *s)
{
	int err;
	const char *cmd;
	size_t cmd_len;

	frame_reset(s->frame_in);

	err = frame_read(s->broker_fd, s->frame_in);
	if (err) {
		return -1;
	}
	
	cmd = s->frame_in->cmd;
	cmd_len = s->frame_in->cmd_len;
	/* heart-beat */
	if (!cmd) {
		return 0;
	}
	
	if (!strncmp(cmd, "CONNECTED", cmd_len)) {
		on_connected(s);
	} else if (!strncmp(cmd, "ERROR", cmd_len)) {
		on_error(s);
	} else if (!strncmp(cmd, "RECEIPT", cmd_len)) {
		on_receipt(s);
	} else if (!strncmp(cmd, "MESSAGE", cmd_len)) {
		on_message(s);
	} else {
		return -1;
	}
	
	return 0;
}

int stomp_run(stomp_session_t *s)
{
	fd_set rd;
	int r;
	struct timeval tv;
	unsigned long t; /* select timeout in milliseconds */
	struct timespec now;
	long elapsed;

	if (!s->broker_hb && !s->client_hb) {
		t = 1000;
	} else if (s->broker_hb && s->client_hb) {
		t = s->broker_hb < s->client_hb ? s->broker_hb : s->client_hb;
	} else {
		t = s->broker_hb > s->client_hb ? s->broker_hb : s->client_hb;
	}
	
	tv.tv_sec = t / 1000;
	tv.tv_usec = (t % 1000) * 1000;
	
	while(s->run) {
		FD_ZERO(&rd);
		FD_SET(s->broker_fd, &rd);
	
		r = select(s->broker_fd + 1, &rd, 0, 0, &tv);
		if(r < 0 && errno != EINTR) {
			goto stomp_run_error;
		} 
	
		if(r && FD_ISSET(s->broker_fd, &rd)) {
			clock_gettime(CLOCK_MONOTONIC, &s->last_read);
			s->broker_timeouts = 0;
			if (on_server_cmd(s)) {
				goto stomp_run_error;
			}
		}

		if (s->callbacks.user) {
			s->callbacks.user(s, NULL, s->ctx);
		}

		if (s->client_hb || s->broker_hb) {
			clock_gettime(CLOCK_MONOTONIC, &now);
		}
		
		if (s->broker_hb) {
			elapsed = (now.tv_sec - s->last_read.tv_sec) * 1000 + \
				  (now.tv_nsec - s->last_read.tv_nsec) / 1000000;

			if (elapsed > s->broker_hb) {
				memcpy(&s->last_read, &now, sizeof(s->last_write));
				s->broker_timeouts++;
			}

			if (s->broker_timeouts > MAXBROKERTMOUTS) {
				errno = ETIMEDOUT;
				goto stomp_run_error;
			}
		}
		
		if (s->client_hb) {
			elapsed = (now.tv_sec - s->last_write.tv_sec) * 1000 + \
				  (now.tv_nsec - s->last_write.tv_nsec) / 1000000;

			if (elapsed > s->client_hb) {
				memcpy(&s->last_write, &now, sizeof(s->last_write));
				if (write(s->broker_fd, "\n", 1) == -1) {
					goto stomp_run_error;
				}
			}
		}
	}

	(void)close(s->broker_fd);
	return 0;

stomp_run_error:

	(void)close(s->broker_fd);
	return -1;
}

