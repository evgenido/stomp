#ifndef STOMP_H
#define STOMP_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef stomp_session_t
 *
 * An opaque STOMP sesstion handle
 *
 * @see stomp_session_new()
 * @see stomp_session_free()
 */
typedef struct _stomp_session stomp_session_t;

typedef struct {
	const char *key; /**< null terminated string */
	const char *val; /**< null terminated string */
} stomp_hdr_t;

typedef struct {
	size_t hdrc; /**< number of headers */
	const stomp_hdr_t *hdrs; /**< pointer to an array of headers */
} stomp_ctx_connected_t;

typedef struct {
	size_t hdrc; /**< number of headers */
	const stomp_hdr_t *hdrs; /**< pointer to an array of headers */
} stomp_ctx_receipt_t;

typedef struct { 
	size_t hdrc; /**< number of headers */
	const stomp_hdr_t *hdrs; /**< pointer to an array of headers */
	const void *body; /**< pointer to the body of the message */
	size_t body_len; /**< length of body in bytes */
} stomp_ctx_error_t;

typedef struct {
	size_t hdrc; /**< number of headers */
	const stomp_hdr_t *hdrs; /**< pointer to an array of headers */
	const void *body; /**< pointer to the body of the message */
	size_t body_len; /**< length of body in bytes */
} stomp_ctx_message_t;

typedef void(* stomp_callback_t)(stomp_session_t *s, void *callback_ctx, void *session_ctx);

typedef enum {
	SCB_CONNECTED,
	SCB_ERROR,
	SCB_MESSAGE,
	SCB_RECEIPT,
	SCB_USER
} stomp_cb_type_t;

void stomp_callback_set(stomp_session_t *s, stomp_cb_type_t type, stomp_callback_t cb);
void stomp_callback_del(stomp_session_t *s, stomp_cb_type_t type);

/**
 * Create a STOMP session handle.
 *
 * @param callbacks Callbacks to run when a certain STOMP event occurs.
 * @param session_ctx A data pointer to pass to the callback.
 * @return a newly allocated session or @c NULL on errors.
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
 *
 * @param s pointer to a session handle
 * @param host
 * @param service
 * @param hdrc
 * @param hdrs
 *
 * @return 0 on success; negative on error and errno is set.
 */
int stomp_connect(stomp_session_t *s, const char *host, const char *service, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Disconnect from a STOMP broker.
 *
 * @param s pointer to a session handle
 * @param hdrc
 * @param hdrs
 *
 * @return 0 on success; negative on error and errno is set.
 */
int stomp_disconnect(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Subscribe to a destination.
 *
 * Headers MUST contain a "destination" header key.
 * Headers SHOULD contain an "id" header with unique key.
 * Headers MAY contan an "ack" header. If none is provided "ack:auto" will be sent.
 *
 * If no "id" header is provided one will be generated. The return value is the key.
 * @return negative on error; or client_id to be used in stomp_unsubscribe()
*/
int stomp_subscribe(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Unsubscribe from a destination.
 *
 * Headers MUST contain a "destination" header key.
 * For Stomp 1.1+, "id" header key per the specifications.  
 * client_id is the handle returned by stomp_subscribe()
 */
int stomp_unsubscribe(stomp_session_t *s, int client_id, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Start a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 */
int stomp_begin(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Abort a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 */
int stomp_abort(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Acknowledge a message.
 *
 * Stomp 1.0 Headers MUST contain a "message-id" header key.
 * Stomp 1.1 Headers must contain a "message-id" key and a "subscription" header key.
 * Stomp 1.2 Headers must contain a unique "id" header key.
 */
int stomp_ack(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Nack a message.
 *
 * Stomp 1.1 Headers must contain a "message-id" key and a "subscription" header key.
 * Stomp 1.2 Headers must contain a unique "id" header key.
 * Disallowed for an established STOMP 1.0 connection.
 */
int stomp_nack(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Commit a transaction.
 *
 * Headers MUST contain a "transaction" header key 
 * with a value that is not an empty string.
 */
int stomp_commit(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs);

/**
 * Send a message.
 *
 * Headers MUST contain a "destination" header key.
 * Headers SHOULD contain a "content-type" header key.
 * Header "content-length" will be set according to body_len parameter.
 */
int stomp_send(stomp_session_t *s, int hdrc, const stomp_hdr_t *hdrs, void *body, size_t body_len);

int stomp_run(stomp_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* STOMP_H */
