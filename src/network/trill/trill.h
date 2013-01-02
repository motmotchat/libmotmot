#ifndef __TRILL_H__
#define __TRILL_H__

#include <stdint.h>
#include <sys/types.h>

/**
 * struct trill_connection - An opaque type representing a connection to another
 * Trill client.
 */
struct trill_connection;

/**
 * trill_callback_t - Callback type for most Trill callbacks.
 *
 * @param conn      The connection upon which to act.
 * @return          0 to cancel the callback, 1 to reschedule it.
 */
typedef int (*trill_callback_t)(struct trill_connection *conn);

/**
 * trill_want_write_callback_t - Callback from Trill to the application's event
 * loop.
 *
 * Called when Trill wishes to receive can-write events.
 *
 * @param cb_data   The callback data pointer set with `trill_set_data`. In most
 *                  cases, some represent of the event loop's representation of
 *                  the connection object and other application-specific state.
 * @return          0 on success, nonzero on error.
 */
typedef int (*trill_want_write_callback_t)(void *cb_data);

/**
 * trill_want_timeout_callback_t - Callback from Trill to the application's
 * event loop.
 *
 * Called when Trill wishes to receive timeout events. These should fire at
 * regular intervals until `callback` returns 0.
 *
 * @param cb_data   The callback data pointer set with `trill_set_data`. In most
 *                  cases, some represent of the event loop's representation of
 *                  the connection object and other application-specific state.
 * @param callback  The Trill function to call periodically. The timer should be
 *                  canceled when this callback returns 0.
 * @param millis    The number of milliseconds between calls.
 * @return          0 on success, nonzero on error.
 */
typedef int (*trill_want_timeout_callback_t)(void *cb_data,
    trill_callback_t callback, unsigned int millis);

/**
 * trill_connected_callback_t - Callback from Trill to the application's event
 * loop.
 *
 * Called when Trill has established a secure connection and is ready to send
 * and receive application data.
 *
 * @param cb_data   The callback data pointer set with `trill_set_data`. In most
 *                  cases, some represent of the event loop's representation of
 *                  the connection object and other application-specific state.
 */
typedef void (*trill_connected_callback_t)(void *cb_data);

/**
 * trill_recv_callback_t - Callback from Trill to the application's event loop.
 *
 * Called upon the receipt of data. The callback is free to do as it wishes with
 * the buffer, however it should make a copy of it if it wishes to retain the
 * data after it returns.
 *
 * @param cb_data   The callback data pointer set with `trill_set_data`. In most
 *                  cases, some represent of the event loop's representation of
 *                  the connection object and other application-specific state.
 * @param data      The data received on the connection.
 * @param len       The length of the data, in bytes.
 * @param seq       A unique sequence number associated with this message.
 */
typedef void (*trill_recv_callback_t)(void *cb_data, void *data, size_t len,
    uint64_t *seq);


/**
 * struct trill_callback_vtable - Various event loop callbacks.
 *
 * See the documentation accompanying the type signatures above.
 */
struct trill_callback_vtable {
  trill_want_write_callback_t want_write_callback;
  trill_want_timeout_callback_t want_timeout_callback;
};

/**
 * trill_init - Initialize Trill.
 *
 * Must be called exactly once before any other trill functions are called.
 *
 * @param vtable    A structure containing various callback types. All necessary
 *                  data will be copied from this struct into other storage, so
 *                  it is not necessary to retain this structure after calling
 *                  `trill_init`.
 */
int trill_init(const struct trill_callback_vtable *vtable);

/**
 * Create a new Trill connection. This initializes the internal state required
 * by a Trill connection, as well as opening a listening UDP socket.
 *
 * @return          A newly initialized Trill connection.
 */
struct trill_connection *trill_connection_new();

/**
 * trill_connection_free - Release the resources of a Trill connection, and
 * close the associated UDP port.
 *
 * @param conn      The connection to free.
 * @return          0 on success, nonzero on error.
 */
int trill_connection_free(struct trill_connection *conn);

/**
 * trill_connect - Connect to another Trill client.
 *
 * @param conn      The connection on which to connect.
 * @param who       The remote hostname to connect to.
 * @param address   The remote address to connect to. Should be an IPv4 address
 *                  in presentation format (see `inet_pton`).
 * @param port      The remote port to connect to.
 * @return          0 on success, nonzero on error.
 */
int trill_connect(struct trill_connection *conn, const char *who,
    const char *address, uint16_t port);

/**
 * trill_get_fd - Get the file descriptor associated with a given connection.
 *
 * @param conn      The connection to examine.
 * @return          A file descriptor of a listening UDP socket.
 */
int trill_get_fd(const struct trill_connection *conn);

/**
 * trill_get_port - Get the ephemeral port used by a given connection.
 *
 * @param conn      The connection to examine.
 * @return          The port the UDP socket is listening on.
 */
uint16_t trill_get_port(const struct trill_connection *conn);

/**
 * trill_set_data - Associate a pointer with a connection.
 *
 * This pointer will be used in every callback Trill makes to the enclosing
 * application, and is useful for storing the application's representation of
 * this connection. This will likely include the event loop's representation of
 * the Trill connection for ease of access.
 *
 * @param conn      The connection to associate data with.
 * @param cb_data   An arbitrary pointer for use in callbacks.
 */
void trill_set_data(struct trill_connection *conn, void *cb_data);

/**
 * trill_set_connected_callback - Set a callback that will be invoked when the
 * connection has been established.
 *
 * @param conn      The connection to associate the callback with.
 * @param callback  The function to call upon connection establishment.
 */
void trill_set_connected_callback(struct trill_connection *conn,
    trill_connected_callback_t callback);

/**
 * trill_set_recv_callback - Set a callback that will be invoked upon the
 * receipt of data.
 *
 * See the documentation for the callback type for more information.
 *
 * @param conn      The connection to associate the callback with.
 * @param callback  The function to call upon the receipt of data.
 */
void trill_set_recv_callback(struct trill_connection *conn,
    trill_recv_callback_t callback);

/**
 * trill_set_key - Set cryptographic credentials for this connection.
 *
 * It is possible to call this function multiple times with multiple sets of
 * credentials.
 *
 * @param conn      The connection to set credentials for.
 * @param key_path  A null-terminated file path containing a private key in PEM
 *                  format.
 * @param cert_path A null-terminated file path containing a certificate or
 *                  certificate chain in PEM format.
 * @return          0 on success, nonzero on error.
 */
int trill_set_key(struct trill_connection *conn, const char *key_path,
    const char *cert_path);

/**
 * trill_set_ca - Set trust root(s) for connection.
 *
 * It is possible to call this function multiple times with multiple trusted
 * certificates.
 *
 * @param conn      The connection to set credentials for.
 * @param ca_path   A null-terminated file path containing a certificate or
 *                  list of certificates in PEM format.
 * @return          0 on success, nonzero on error.
 */
int trill_set_ca(struct trill_connection *conn, const char *ca_path);

/**
 * trill_can_read - Notify Trill that the connection is available for reading.
 *
 * @param conn      The connection that has bytes available for reading.
 * @return          0 to cancel this callback, 1 to reschedule it.
 */
int trill_can_read(struct trill_connection *conn);

/**
 * trill_can_write - Notify Trill that the connection is available for writing.
 *
 * @param conn      The connection that has bytes available for writing.
 * @return          0 to cancel this callback, 1 to reschedule it.
 */
int trill_can_write(struct trill_connection *conn);

/**
 * trill_send - Send data on a Trill connection
 *
 * @param conn      The connection to send data over.
 * @param data      The data to send.
 * @param len       The number of bytes to send.
 * @return          The number of bytes sent, or -1 on error.
 *                  An attempt has been made to set errno in a similar fashion
 *                  to how send(2) sets it on error. In particular, EAGAIN and
 *                  EINTR will be reported as normal.
 */
ssize_t trill_send(struct trill_connection *conn, const void *data, size_t len);

#endif // __TRILL_H__
