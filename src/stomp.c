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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#include <stomp/stomp.h>
#include <stomp/frame.h>
#include <stomp/hdr.h>

/* enough space for ULLONG_MAX as string */
#define MAXBUFLEN 25

/* max number of broker heartbeat timeouts */
#define MAXBROKERTMOUTS 5

static int parse_version(const char *s, enum stomp_prot *v)
{
	enum stomp_prot tmp_v;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	if (!strncmp(s, "1.2", 3)) {
		tmp_v = SPL_12;
	} else if (!strncmp(s, "1.1", 3)) {
		tmp_v = SPL_11;
	} else if (!strncmp(s, "1.0", 3)) {
		tmp_v = SPL_10;
	} else {
		tmp_v = SPL_10;
	}
	
	*v = tmp_v;

	return 0;
}
static int parse_heartbeat(const char *s, unsigned long *x, unsigned long *y)
{
	unsigned long tmp_x, tmp_y;
	char *endptr;
	const char *nptr = s;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	tmp_x = strtoul(nptr, &endptr, 10);
	if (errno != 0) {
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
	tmp_y = strtoul(nptr, &endptr, 10);
	if (errno !=0) {
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

void stomp_callback_set(stomp_session_t *s, enum stomp_cb_type type, stomp_cb_t cb)
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

void stomp_callback_del(stomp_session_t *s, enum stomp_cb_type type)
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


int stomp_connect(stomp_session_t *s, const char *host, const char *service, size_t hdrc, const struct stomp_hdr *hdrs)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd;
	int err;
	unsigned long x = 0;
	unsigned long y = 0;
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

int stomp_disconnect(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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
int stomp_subscribe(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_unsubscribe(stomp_session_t *s, int client_id, size_t hdrc, const struct stomp_hdr *hdrs)
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
int stomp_begin(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_abort(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_ack(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_nack(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_commit(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs)
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

int stomp_send(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs, void *body, size_t body_len)
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
	struct stomp_ctx_connected e;
	frame_t *f = s->frame_in;
	unsigned long x, y;
	const char *h;
	size_t hdrc;
	const struct stomp_hdr *hdrs;
	enum stomp_prot v;

	hdrc = frame_hdrs_get(f, &hdrs);
	h = hdr_get(hdrc, hdrs, "version");
	if (h && !parse_version(h, &v)) {
		s->protocol = v;
	}

	h = hdr_get(hdrc, hdrs, "heart-beat");
	if (h && !parse_heartbeat(h, &x, &y)) {
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

	e.hdrc = hdrc;
	e.hdrs = hdrs;

	s->callbacks.connected(s, &e, s->ctx);
}

static void on_receipt(stomp_session_t *s) 
{ 
	struct stomp_ctx_receipt e;
	frame_t *f = s->frame_in;

	if (!s->callbacks.receipt) {
		return;
	}

	e.hdrc = frame_hdrs_get(f, &e.hdrs);

	s->callbacks.receipt(s, &e, s->ctx);
}

static void on_error(stomp_session_t *s) 
{ 
	struct stomp_ctx_error e;
	frame_t *f = s->frame_in;

	if (!s->callbacks.error) {
		return;
	}

	e.hdrc = frame_hdrs_get(f, &e.hdrs);
	e.body_len = frame_body_get(f, &e.body);

	s->callbacks.error(s, &e, s->ctx);
}

static void on_message(stomp_session_t *s) 
{ 
	struct stomp_ctx_message e;
	frame_t *f = s->frame_in;

	if (!s->callbacks.message) {
		return;
	}
	
	e.hdrc = frame_hdrs_get(f, &e.hdrs);
	e.body_len = frame_body_get(f, &e.body);

	s->callbacks.message(s, &e, s->ctx);
}

static int on_server_cmd(stomp_session_t *s)
{
	int err;
	const char *cmd;
	size_t cmd_len;
	frame_t *f = s->frame_in;

	frame_reset(f);

	err = frame_read(s->broker_fd, f);
	if (err) {
		return -1;
	}
	
	cmd_len = frame_cmd_get(f, &cmd);
	/* heart-beat */
	if (!cmd_len) {
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
	unsigned long elapsed;

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
