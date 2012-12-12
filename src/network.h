#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <stdint.h>
#include <netinet/in.h>

struct trill_connection;
struct trill_crypto_session;
struct trill_crypto_identity;

// Returns zero if the callback wishes to be removed, non-zero otherwise
typedef int (*trill_net_cb_t)(struct trill_connection *conn);

typedef int (*trill_net_want_write_cb_t)(struct trill_connection *conn);
// XXX: will we ever want two timeouts simultaneously active for a single
// connection? If so, we might need an additional tag somewhere. But we might
// just be able to structure things such that timeout callbacks are idempotent
typedef int (*trill_net_want_timeout_cb_t)(struct trill_connection *conn,
    unsigned millis);

trill_net_want_write_cb_t trill_net_want_write_cb;
trill_net_want_timeout_cb_t trill_net_want_timeout_cb;

enum trill_connection_state {
  TC_STATE_INIT,              // Connection has been created
  TC_STATE_PROBING,           // Connection has a remote, is probing them
  TC_STATE_PRESHAKE_SERVER,   // We think we're connected and are the server
  TC_STATE_PRESHAKE_CLIENT,   // We think we're connected and are the client
  TC_STATE_HANDSHAKE_SERVER,  // We have a connection, and we are the server
  TC_STATE_HANDSHAKE_CLIENT,  // We have a connection, and we are the client
  TC_STATE_ENCRYPTED          // An encrypted channel has been established
};

struct trill_connection {
  int tc_sock_fd;                 // A bound listening UDP socket
  void *tc_event_loop_descriptor; // An opaque pointer that represents this
                                  // connection on the event loop
  uint16_t tc_port;               // The local UDP port we're listening on
  enum trill_connection_state tc_state;

  uint32_t tc_server_priority[2]; // A "64-bit" priority number

  // Connection event callback vtable
  trill_net_cb_t tc_can_read_cb;
  trill_net_cb_t tc_can_write_cb;
  trill_net_cb_t tc_timeout_cb;

  struct trill_crypto_session *tc_crypto;
  struct trill_crypto_identity *tc_id;
};

/**
 * Initialize trill global state. Call this once at the start of your program,
 * before calling any other trill networking functions.
 *
 * Pass this function two function pointers, the first of which Trill will call
 * when it wants to schedule receiving more write callbacks, and the second when
 * it wants to schedule a new timeout.
 */
int trill_net_init(trill_net_want_write_cb_t want_write,
    trill_net_want_timeout_cb_t want_timeout);

/**
 * Allocate and initialize a new listening trill connection.
 *
 * Returns NULL on error, or a pointer to a trill_connection struct with a
 * populated file descriptor
 */
struct trill_connection *trill_connection_new(struct trill_crypto_identity *id);

/**
 * "Connect" to a new remote. This really just initializes the tc_remote field
 * in the trill_connection struct -- no data is actually sent.
 */
int trill_connection_connect(struct trill_connection *conn, const char *addr,
    uint16_t port);

/**
 * Free a trill connection. Also closes the associated socket.
 */
int trill_connection_free(struct trill_connection *conn);



// TODO: documentation
int trill_connection_probe(struct trill_connection *conn);
int trill_connection_read_probe(struct trill_connection *conn);
int trill_connection_probe_ok(struct trill_connection *conn);
int trill_connection_go_ahead(struct trill_connection *conn);
int trill_connection_tls_read(struct trill_connection *conn);
int trill_connection_tls_write(struct trill_connection *conn);

int trill_crypto_session_init(struct trill_connection *conn);

#endif // __NETWORK_H__
