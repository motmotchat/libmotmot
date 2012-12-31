/**
 * plume.h - Plume client interface.
 */
#ifndef __TRILL_CLIENT_PLUME_H__
#define __TRILL_CLIENT_PLUME_H__

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
 * @param data      User data associated with the client connection.
 */
typedef int (*plume_callback_t)(struct plume_client *client,
    enum plume_status status, void *data);

/**
 * plume_recv_callback_t - Callback type
 */

/**
 * trill_connect_plume - Connect to a Plume server.
 *
 * @param handle    String specifying the identity under which the client
 *                  wishes to connect.  Should have the form username@domain.
 * @param size      Size of the handle, in bytes.
 * @param vtable    Set of callbacks
 * @return          Opaque struct, to be passed back
 */
struct plume_client *plume_connect(char *handle, size_t size);

void plume_authenticate(char *, char *);

#endif /* __TRILL_CLIENT_PLUME_H__ */
