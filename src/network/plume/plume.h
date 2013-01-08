/**
 * plume.h - Plume client interface.
 */
#ifndef __PLUME_H__
#define __PLUME_H__

#include <stdlib.h>

/**
 * struct plume_client - An opaque type representing a client connection to a
 * Plume server.
 */
struct plume_client;

/**
 * enum plume_status - Success/error statuses returned from library calls or
 * passed to application-supplied callbacks.
 *
 * 0 always indicates success; nonzero indicates error.  Positive values are
 * Plume-defined errors, whereas negative errors should be negated and
 * interpreted as errno errors.
 */
enum plume_status {
  PLUME_SUCCESS = 0,
  PLUME_EINUSE,
  PLUME_ENOMEM,
  PLUME_EFILE,
  PLUME_EIDENTITY,
  PLUME_EDNS,
  PLUME_EFATAL,
};


///////////////////////////////////////////////////////////////////////////////
//
//  Callback types.
//

/**
 * plume_connect_callback_t - Callback type for notifying the client that a
 * connect call has completed.
 *
 * @param client    The client object for which action was completed or is
 *                  required.
 * @param status    Status of the completed action (or PLUME_SUCCESS if no
 *                  relevant action exists.
 * @param data      User data associated with the client object.
 */
typedef void (*plume_connect_callback_t)(struct plume_client *client,
    int status, void *data);

/**
 * plume_recv_callback_t - Callback type for notifying the client of received
 * data.
 *
 * @param client    The client object for which data was received.
 * @param buf       The data received from the server.
 * @param len       The size of the data, in bytes.
 * @param data      User data associated with the client object.
 */
typedef void (*plume_recv_callback_t)(struct plume_client *client,
    void *buf, size_t len, void *data);


///////////////////////////////////////////////////////////////////////////////
//
//  Main interface.
//

/**
 * trill_init - Initialize the Plume client service.  May be called more than
 * once, but only the first call will be functional.
 *
 * @return          0 on success, nonzero on failure.
 */
int plume_init(void);

/**
 * plume_client_new - Instantiate a new Plume client object.
 *
 * @param cert_path The path to the client's identity cert.  Should be a
 *                  null-terminated string.
 * @return          A new Plume client object.
 */
struct plume_client *plume_client_new(const char *cert_path);

/**
 * plume_client_destroy - Destroy a Plume client object, closing the raw socket
 * connection to the Plume server.
 *
 * @param client    The client to deinit and free.
 * @return          0 on success, nonzero on failure.
 */
int plume_client_destroy(struct plume_client *client);

/**
 * plume_connect_server - Connect to a Plume server.
 *
 * When success or failure of the connection process is known, the pc_connect
 * callback set on `client' will be called to notify the caller.
 *
 * No work should be performed on the client until the connected callback is
 * called.  Failure to abide may result in inconsistent state for the duration
 * of the Plume client service.
 *
 * @param client    The Plume client object to use for the connection.
 */
void plume_connect_server(struct plume_client *client);


///////////////////////////////////////////////////////////////////////////////
//
//  Utility routines.
//

/**
 * plume_client_get_fd - Get the socket file description supporting the server
 * connection.
 *
 * @param client    The client object for which to fetch the fd.
 * @return          The socket fd.
 */
int plume_client_get_fd(const struct plume_client *client);

/**
 * plume_client_set_data - Set associated user data.
 *
 * @param client    The client object on which to associate data.
 * @param data      Opaque data pointer.  Will never be dereferenced by the
 *                  Plume client.
 */
void plume_client_set_data(struct plume_client *client, void *data);

/**
 * plume_client_set_connect_cb - Set the connected client callback.
 *
 * @param client    The client object on which to set the callback.
 * @param cb        The callback to set.
 */
void plume_client_set_connect_cb(struct plume_client *client,
    plume_connect_callback_t cb);

/**
 * plume_client_set_recv_cb - Set the recv client callback.
 *
 * @param client    The client object on which to set the callback.
 * @param cb        The callback to set.
 */
void plume_client_set_recv_cb(struct plume_client *client,
    plume_recv_callback_t cb);

/**
 * plume_client_set_key - Set cryptographic credentials for this connection.
 *
 * It is possible to call this function multiple times with multiple sets of
 * credentials.
 *
 * @param client    The client to set credentials for.
 * @param key_path  A null-terminated file path containing a private key in PEM
 *                  format.
 * @param cert_path A null-terminated file path containing a certificate or
 *                  certificate chain in PEM format.
 * @return          0 on success, nonzero on error.
 */
int plume_client_set_key(struct plume_client *client, const char *key_path,
    const char *cert_path);

/**
 * plume_client_set_ca - Set trust root(s) for connection.
 *
 * It is possible to call this function multiple times with multiple trusted
 * certificates.
 *
 * @param client    The client to set credentials for.
 * @param ca_path   A null-terminated file path containing a certificate or
 *                  list of certificates in PEM format.
 * @return          0 on success, nonzero on error.
 */
int plume_client_set_ca(struct plume_client *client, const char *ca_path);

#endif /* __PLUME_H__ */
