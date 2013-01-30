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
