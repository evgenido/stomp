#ifndef FRAME_H
#define FRAME_H

#include "stomp.h"

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

frame_t *frame_new();
void frame_free(frame_t *f);
void frame_reset(frame_t *f);
size_t buflene(const void *data, size_t len);
void *frame_alloc(frame_t *f, size_t len);
void *frame_bufcat(frame_t *f, const void *data, size_t len);
void *frame_bufcate(frame_t *f, const void *data, size_t len);
int frame_cmd_set(frame_t *f, const char *cmd);
int frame_hdr_add(frame_t *f, const char *key, const char *val);
int frame_hdrs_add(frame_t *f, int hdrc, const stomp_hdr_t *hdrs);
int frame_body_set(frame_t *f, const void *data, size_t len);
int frame_read(int fd, frame_t *f);
int frame_write(int fd, frame_t *f);

#endif /* FRAME_H */
