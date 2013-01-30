#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stomp.h>

typedef struct {
	const char *destination;
} ctx_t;

static void dump_hdrs(int hdrc, const stomp_hdr_t *hdrs)
{
	int i;
	for (i=0; i < hdrc; i++) {
		fprintf(stdout, "%s:%s\n", hdrs[i].key, hdrs[i].val);
	}
}

static void _connected(stomp_session_t *s, void *ctx, void *session_ctx)
{
	ctx_t *c = session_ctx;
	stomp_hdr_t hdrs[] = {
		{"destination", c->destination},
	};

	int err = stomp_subscribe(s, sizeof(hdrs)/sizeof(stomp_hdr_t), hdrs);
	if (err<0) {
		perror("stomp");
	}
}

static void _message(stomp_session_t *s, void *ctx, void *session_ctx)
{
	stomp_ctx_message_t *e = ctx;
	dump_hdrs(e->hdrc, e->hdrs);
	fprintf(stdout, "message: %s\n", (const char *)e->body);
}

static void _error(stomp_session_t *session, void *ctx, void *session_ctx)
{
	stomp_ctx_error_t *e = ctx;
	dump_hdrs(e->hdrc, e->hdrs);
	fprintf(stderr, "err: %s\n", (const char *)e->body);
}

static void _receipt(stomp_session_t *s, void *ctx, void *session_ctx)
{
	stomp_ctx_receipt_t *e = ctx;
	dump_hdrs(e->hdrc, e->hdrs);
	fprintf(stdout, "receipt: \n");
}

static void _user(stomp_session_t *s, void *ctx, void *session_ctx) 
{
}

int main(int argc, char *argv[]) 
{
	int err;
	ctx_t c;
	stomp_session_t *s;
	stomp_hdr_t hdrs[] = {
		{"login", "admin"},
		{"passcode", "password"},
		{"accept-version", "1.2"},
		{"heart-beat", "1000,1000"},
	};

	if (argc != 2) {
		fprintf(stderr, "usage: %s <destination>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	c.destination = argv[1];

	s = stomp_session_new(&c);
	if (!s) {
		perror("stomp");
		exit(EXIT_FAILURE);
	}

	stomp_callback_set(s, SCB_CONNECTED, _connected);
	stomp_callback_set(s, SCB_ERROR, _error);
	stomp_callback_set(s, SCB_MESSAGE, _message);
	stomp_callback_set(s, SCB_RECEIPT, _receipt);
	stomp_callback_set(s, SCB_USER, _user);

	err = stomp_connect(s, "127.0.0.1", "61613", sizeof(hdrs)/sizeof(stomp_hdr_t), hdrs);
	if (err) {
		perror("stomp");
		stomp_session_free(s);
		exit(EXIT_FAILURE);
	}
	
	err = stomp_run(s);
	if (err) {
		perror("stomp");
		stomp_session_free(s);
		exit(EXIT_FAILURE);
	}

	stomp_session_free(s);

	exit(EXIT_SUCCESS);
}

