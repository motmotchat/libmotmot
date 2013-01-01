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
 * enum plume_status - Success/error statuses passed to user-supplied Plume
 * callbacks.
 */
enum plume_status {
  PLUME_SUCCESS,
};

/**
 * plume_callback_t - Callback type for most Plume client callbacks.  Primarily
 * used for notifying the user of some completed action.
 *
 * @param client    The client object for which action was completed or is
 *                  required.
 * @param status    Status of the completed action (or PLUME_SUCCESS if no
 *                  relevant action exists.
 * @param data      User data associated with the client object.
 */
typedef void (*plume_callback_t)(struct plume_client *client,
    enum plume_status status, void *data);

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

/**
 * plume_client_new - Instantiate a new Plume client object.
 *
 * @return          A new Plume client object.
 */
struct plume_client *plume_client_new(void);

/**
 * plume_client_destroy - Destroy a Plume client object, closing the raw socket
 * connection to the Plume server.
 *
 * @param client    The client to deinit and free.
 * @return          0 on success, nonzero on failure.
 */
int plume_client_destroy(struct plume_client *client);

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
 * @param connect   The callback to set.
 */
void plume_client_set_connect_cb(struct plume_client *client,
    plume_callback_t connect);

/**
 * plume_client_set_recv_cb - Set the recv client callback.
 *
 * @param client    The client object on which to set the callback.
 * @param recv      The callback to set.
 */
void plume_client_set_recv_cb(struct plume_client *client,
    plume_recv_callback_t recv);

/**
 * plume_connect_server - Connect to a Plume server.
 *
 * When success or failure of the connection process is known, the pc_connect
 * callback set on `client' will be called to notify the caller.
 *
 * @param client    The Plume client object to use for the connection.
 * @param cert_path The path to the cert representing the client's identity.
 *                  Should be a null-terminated string.
 */
void plume_connect_server(struct plume_client *client, const char *cert_path);

#endif /* __PLUME_H__ */
