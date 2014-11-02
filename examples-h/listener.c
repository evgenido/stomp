#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stomp.h>

struct ctx {
    const char *destination;
};

static void dump_hdrs(int hdrc, const struct stomp_hdr *hdrs)
{
    int i;
    for (i = 0; i < hdrc; i++) {
        fprintf(stdout, "%s:%s\n", hdrs[i].key, hdrs[i].val);
    }
}

static void _connected(stomp_session_t *s, void *ctx, void *session_ctx)
{
    struct ctx *c = session_ctx;

    struct stomp_hdr opt_hdrs[] = {
        {"selector", "target = 'mee'"},
        {NULL, NULL},
    };
    int err = stomp_subscribe_h(s, c->destination, opt_hdrs);
    if (err < 0) {
        perror("stomp");
    }
}

static void _message(stomp_session_t *s, void *ctx, void *session_ctx)
{
    struct stomp_ctx_message *e = ctx;
    dump_hdrs(e->hdrc, e->hdrs);
    fprintf(stdout, "message: %s\n", (const char *)e->body);
}

static void _error(stomp_session_t *session, void *ctx, void *session_ctx)
{
    struct stomp_ctx_error *e = ctx;
    dump_hdrs(e->hdrc, e->hdrs);
    fprintf(stderr, "err: %s\n", (const char *)e->body);
}

static void _receipt(stomp_session_t *s, void *ctx, void *session_ctx)
{
    struct stomp_ctx_receipt *e = ctx;
    dump_hdrs(e->hdrc, e->hdrs);
    fprintf(stdout, "receipt: \n");
}

static void _user(stomp_session_t *s, void *ctx, void *session_ctx)
{
    // called every heartbeet
    fprintf(stdout, "hartbeet: %d\n", (int)clock());
}

int main(int argc, char *argv[])
{
    int err;
    struct ctx c;
    stomp_session_t *s;

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

    err = stomp_connect_h(s, "localhost", "61613", NULL, "mybroker", "admin", "password", "5000,10000", NULL);
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

