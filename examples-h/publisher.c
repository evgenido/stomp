#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stomp.h>


int main(int argc, char *argv[])
{
    int err;
    stomp_session_t *s;

    // destination, msg
    if (argc != 3) {
        fprintf(stderr, "usage: %s <destination> <message>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *destination = argv[1];
    const char *msg = argv[2];

    // session initialize
    s = stomp_session_new(NULL);
    if (!s) {
        perror("stomp");
        exit(EXIT_FAILURE);
    }

    // connect
    err = stomp_connect_h(s, "localhost", "61613", NULL, "mybroker", "admin", "password", NULL, NULL);
    if (err) {
        perror("stomp");
        stomp_session_free(s);
        exit(EXIT_FAILURE);
    }

    // send message
    struct stomp_hdr opt_hdrs[] = {
        {"target", "mee"},
        {NULL, NULL},
    };
    err = stomp_send_h(s, destination, opt_hdrs, msg);
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

