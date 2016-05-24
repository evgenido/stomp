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
#ifndef STOMP_H
#define STOMP_H

#include <sys/types.h>
#include <stomp/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

enum stomp_prot {
	SPL_10,
	SPL_11,
	SPL_12
};

typedef struct _stomp_session stomp_session_t;

struct stomp_callbacks {
	void(*connected)(stomp_session_t *s, void *callback_ctx, void *session_ctx);
	void(*message)(stomp_session_t *s, void *callback_ctx, void *session_ctx);
	void(*error)(stomp_session_t *s, void *callback_ctx, void *session_ctx);
	void(*receipt)(stomp_session_t *s, void *callback_ctx, void *session_ctx);
	void(*user)(stomp_session_t *s, void *callback_ctx, void *session_ctx);
};

/**
 * An opaque STOMP sesstion handle
 *
 * @see stomp_session_new()
 * @see stomp_session_free()
 */
struct _stomp_session {
	struct stomp_callbacks callbacks; /* event callbacks */
	void *ctx; /* pointer to user supplied session context */

	frame_t *frame_out; /* library -> broker */
	frame_t *frame_in; /* broker -> library */

	enum stomp_prot protocol;
	int broker_fd;
	int client_id; /* unique ids for subscribe */
	unsigned long client_hb; /* client heart beat period in milliseconds */
	unsigned long broker_hb; /* broker heart beat period in milliseconds */
	struct timespec last_write;
	struct timespec last_read;
	int broker_timeouts; 
	int run;
};

/**
 * Structure representing a STOMP header entry
 *
 * All functions which accept headers as a parameter use this, and 
 * also all context structures.
 */
struct stomp_hdr {
	const char *key; /**< null terminated string */
	const char *val; /**< null terminated string */
};

/**
 * This structure is provided to the client code
 * which registered a callback for SCB_CONNECTED.
 */
struct stomp_ctx_connected {
	size_t hdrc; /**< number of headers */
	const struct stomp_hdr *hdrs; /**< pointer to an array of headers */
};

/**
 * This structure is provided to the client code
 * which registered a callback for SCB_RECEIPT.
 */
struct stomp_ctx_receipt {
	size_t hdrc; /**< number of headers */
	const struct stomp_hdr *hdrs; /**< pointer to an array of headers */
};


/**
 * This structure is provided to the client code
 * which registered a callback for SCB_ERROR.
 */
struct stomp_ctx_error { 
	size_t hdrc; /**< number of headers */
	const struct stomp_hdr *hdrs; /**< pointer to an array of headers */
	const void *body; /**< pointer to the body of the message */
	size_t body_len; /**< length of body in bytes */
};

/**
 * This structure is provided to the client code
 * which registered a callback for SCB_MESSAGE.
 */
struct stomp_ctx_message {
	size_t hdrc; /**< number of headers */
	const struct stomp_hdr *hdrs; /**< pointer to an array of headers */
	const void *body; /**< pointer to the body of the message */
	size_t body_len; /**< length of body in bytes */
};


/**
 * List of events the client code can register 
 * a callback for.
 *
 * Aside from the server responses the client
 * can also register for a user callback (SCB_USER). The callback
 * will be called within every itteration of stomp_run(). 
 * The amount of time between the calls is set to 1 second when no 
 * heart-beat header is provided. Otherwise it wil get called 
 * with the smallest timeneeded to satisfy the requuired heart-beats.
 *
 * @seen stomp_callback_set
 * @seen stomp_callback_del
 */
enum stomp_cb_type {
	SCB_CONNECTED, /**< server sended CONNECTED */
	SCB_ERROR, /**< server sended ERROR */
	SCB_MESSAGE, /**< server sended MESSAGE  */
	SCB_RECEIPT, /**< server sended RECEIPT  */
	SCB_USER /**< user slot */
};

typedef void(*stomp_cb_t)(stomp_session_t *s, void *callback_ctx, void *session_ctx);

/**
 * Register a callback when a particular event occurs.
 *
 * @param s pointer to a session handle
 * @param type type of event to register for
 * @param cb callback to register
 */
void stomp_callback_set(stomp_session_t *s, enum stomp_cb_type type, stomp_cb_t cb);

/**
 * Delete callback for a particular event.
 *
 * @param s Pointer to a session handle
 * @param type Type of event to register for
 * @param cb Callback to register
 */
void stomp_callback_del(stomp_session_t *s, enum stomp_cb_type type);

/**
 * Create a STOMP session handle.
 *
 * @param callbacks Callbacks to run when a certain STOMP event occurs.
 * @param session_ctx A data pointer to pass to the callback.
 * @return a newly allocated session or NULL on errors.
 *
 * @see stomp_session_free()
 */
stomp_session_t *stomp_session_new(void *session_ctx);

/**
 * Delete a STOMP session handle.
 *
 * @param session Session handle to delete.
 *
 * @see stomp_session_new()
 */
void stomp_session_free(stomp_session_t *s);

/**
 * Connect to a STOMP broker.
 *
 * Headers parameter MUST contain the headers required by the specification.
 * Note that in order to get notified of the server responses you must
 * register the appropriate handler and call stomp_run()
 *
 * @param s Pointer to a session handle.
 * @param host Hostname to connect to.
 * @param service Service of port to connect to.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_connect(stomp_session_t *s, const char *host, const char *service, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Disconnect from a STOMP broker.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_disconnect(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Subscribe to a destination.
 *
 * Headers MUST contain a "destination" header key.
 * Headers SHOULD contain an "id" header with unique key.
 * Headers MAY contan an "ack" header. If none is provided "ack:auto" will be sent.
 *
 * If no "id" header is provided one will be generated. The return value is the key, and it must be 
 * fed to stomp_unsibsribe().
 *
 * Note that if an "id" header si provided no attempt will be made
 * to assure the uniqueness of the value.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return negative on error; or client_id to be used in stomp_unsubscribe()
*/
int stomp_subscribe(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Unsubscribe from a destination.
 *
 * Headers MUST contain a "destination" header key.
 * For Stomp 1.1+, "id" header key per the specifications.  
 * client_id is the handle returned by stomp_subscribe()
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_unsubscribe(stomp_session_t *s, int client_id, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Start a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_begin(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Abort a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_abort(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Acknowledge a message.
 *
 * Stomp 1.0 Headers MUST contain a "message-id" header key.
 * Stomp 1.1 Headers must contain a "message-id" key and a "subscription" header key.
 * Stomp 1.2 Headers must contain a unique "id" header key.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_ack(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Nack a message.
 *
 * Stomp 1.1 Headers must contain a "message-id" key and a "subscription" header key.
 * Stomp 1.2 Headers must contain a unique "id" header key.
 * Disallowed for an established STOMP 1.0 connection.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_nack(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Commit a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_commit(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs);

/**
 * Send a message.
 *
 * Headers MUST contain a "destination" header key.
 * Headers SHOULD contain a "content-type" header key.
 * Header "content-length" will be set according to body_len parameter.
 *
 * @param s Pointer to a session handle.
 * @param hdrc Number of STOMP headers.
 * @param hdrs Pointer to an array of STOMP headers.
 * @param body Pointer to the message body.
 * @param body_len Length of the body in bytes.
 *
 * @return 0 on success; negative on error and errno is set appropriately.
 */
int stomp_send(stomp_session_t *s, size_t hdrc, const struct stomp_hdr *hdrs, void *body, size_t body_len);

/**
 * Runs the library main loop.
 * 
 * This function will not return until either he server closes the 
 * connection, or the client calls stomp_disconnect()
 *
 * @param s Pointer to a session handle.
 *
 * @return 0 on success; negative on error and errno is set appropriately
 */
int stomp_run(stomp_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* STOMP_H */
