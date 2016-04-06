#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    struct stomp_ctx_connected *e = ctx;
    dump_hdrs(e->hdrc, e->hdrs);
    fprintf(stdout, "connected: \n");
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
}

int main(int argc, char *argv[])
{
    int err;
    struct ctx c;
    stomp_session_t *s;

    // destination
    if (argc != 2) {
        fprintf(stderr, "usage: %s <destination>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    c.destination = argv[1];

    // session initialize
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

    // connect
    struct stomp_hdr conn_hdrs[] = {
        {"login", "admin"},
        {"passcode", "password"},
        {"accept-version", "1.2"},
        {"heart-beat", "1000,1000"},
    };
    err = stomp_connect(s, "127.0.0.1", "61613", sizeof(conn_hdrs) / sizeof(struct stomp_hdr), conn_hdrs);
    if (err) {
        perror("stomp");
        stomp_session_free(s);
        exit(EXIT_FAILURE);
    }

    // send message
    struct stomp_hdr send_hdrs[] = {
        {"destination", c.destination},
        {"content-type", "text/plain"},
    };
    const char *send_body = "hello message from c";
    err = stomp_send(s, sizeof(send_hdrs) / sizeof(struct stomp_hdr), send_hdrs, (void *)send_body, strlen(send_body));
    if (err) {
        perror("stomp");
        stomp_session_free(s);
        exit(EXIT_FAILURE);
    }

    // disconnect

    // clean
    stomp_session_free(s);

    // exit
    exit(EXIT_SUCCESS);
}

