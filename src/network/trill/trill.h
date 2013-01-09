/**
 * trill.h - Trill public interface.
 */
#ifndef __TRILL_H__
#define __TRILL_H__

#include <stdint.h>
#include <sys/types.h>

#include "event/event.h"

/**
 * struct trill_connection - An opaque type representing a connection to another
 * Trill client.
 */
struct trill_connection;

/**
 * enum trill_status - Success/error statuses returned from library calls or
 * passed to application-supplied callbacks.
 */
enum trill_status {
  TRILL_SUCCESS = 0,
  TRILL_ETIED,
  TRILL_ETLS,
  TRILL_EFATAL,
};


///////////////////////////////////////////////////////////////////////////////
//
//  Callback types.
//

/**
 * trill_status_callback_t - Callback type for notifying the application of the
 * status of a library call on a connection.
 *
 * @param conn      The connection.
 * @param status    Status of the action.  This is an enum trill_status if
 *                  nonnegative; else, it is a negative errno.
 * @param data      User data associated with the connection.
 */
typedef void (*trill_status_callback_t)(struct trill_connection *conn,
    int status, void *data);

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
    uint64_t seq);


///////////////////////////////////////////////////////////////////////////////
//
//  Main interface.
//

/**
 * trill_init - Initialize Trill.  Must be called exactly once before any other
 * Trill functions are called.
 *
 * @return          0 on success, nonzero on failure.
 */
int trill_init(void);

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
 * trill_send - Send data on a Trill connection
 *
 * @param conn      The connection to send data over.
 * @param data      The data to send.
 * @param len       The number of bytes to send.
 * @return          The number of bytes sent, or -1 on error.  An attempt has
 *                  been made to set errno in a similar fashion to how send(2)
 *                  sets it on error.  In particular, EAGAIN and EINTR will be
 *                  reported as normal.
 */
ssize_t trill_send(struct trill_connection *conn, const void *data, size_t len);


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

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
 * trill_set_connect_cb - Set a callback that will be invoked when a connection
 * attempt completes or fails.
 *
 * @param conn      The connection to associate the callback with.
 * @param cb        The function to call upon connection establishment or
 *                  failure.
 */
void trill_set_connect_cb(struct trill_connection *conn,
    trill_status_callback_t callback);

/**
 * trill_set_recv_callback - Set a callback that will be invoked upon the
 * receipt of data.
 *
 * See the documentation for the callback type for more information.
 *
 * @param conn      The connection to associate the callback with.
 * @param cb        The function to call upon the receipt of data.
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

#endif // __TRILL_H__
