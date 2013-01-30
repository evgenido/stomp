#ifndef FRAME_H
#define FRAME_H

#include "stomp.h"

typedef struct _frame frame_t;

frame_t *frame_new();
void frame_free(frame_t *f);
void frame_reset(frame_t *f);
int frame_cmd_set(frame_t *f, const char *cmd);
int frame_hdr_add(frame_t *f, const char *key, const char *val);
int frame_hdrs_add(frame_t *f, int hdrc, const stomp_hdr_t *hdrs);
int frame_body_set(frame_t *f, const void *body, size_t len);
int frame_write(int fd, frame_t *f);

int frame_cmd_get(frame_t *f, const char **cmd);
int frame_hdr_get(frame_t *f, const char *key, const char **val);
int frame_hdrs_get(frame_t *f, const stomp_hdr_t **hdrs);
int frame_body_get(frame_t *f, const void **body);
int frame_read(int fd, frame_t *f);

#endif /* FRAME_H */
