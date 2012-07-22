/**
 * motmot.h - the public API for libmotmot
 */

#ifndef __MOTMOT_H__
#define __MOTMOT_H__

#include <stddef.h>
#include <glib.h>

/**
 * motmot_connect_continuation_t - Continuation invoked when a request for the
 * client to establish a connection returns.
 *
 * @param channel   The GIOChannel that is established; should be NULL on
 *                  failure.
 * @param data      Data used by the continuation that is provided in the
 *                  motmot_connect_callback structure.
 * @returns         0 on success, nonzero on error.
 */
typedef int (*motmot_connect_continuation_t)(GIOChannel *channel, void *data);

/**
 * motmot_connect_cb - Callback object used by clients to signal that a
 * connection has been established; passed as an argument to connect_t.
 */
struct motmot_connect_cb {
  motmot_connect_continuation_t func;
  void *data;
};

/**
 * connect_t - Connection establishment callback type.
 *
 * @param alias     String handle recognized by the client that uniquely
 *                  identifies an individual to connect to.
 * @param size      Length of the alias.
 * @param cb        A callback to be used when the connection attempt
 *                  completes.
 * @returns         0 on success, nonzero on error.
 */
typedef int (*connect_t)(const char *alias, size_t size,
    struct motmot_connect_cb *cb);

/**
 * learn_t - Learning callback type.
 *
 * @param message   The message to learn.  For chats, this is the chat message
 *                  itself.  It is not used for joins and parts.
 * @param len       The size of that message.
 * @param alias     String handle identifying a client.  For chats, it is the
 *                  message source; for joins and parts, it is the joining or
 *                  parting client.
 * @param size      Length of the alias.
 * @param data      Data pointer used by the client to identify the session.
 * @returns         0 on success, nonzero on error.
 */
typedef int (*learn_t)(const void *message, size_t len, const char *alias,
    size_t size, void *data);

/**
 * enter_t - Chatroom entrance notification callback type.
 *
 * @param data      Pointer to motmot's internal session data.  This object
 *                  should be treated as opaque by the client and should be
 *                  passed as an argument to relevant motmot functions.
 * @returns         Data pointer used by the client to identify the session.
 */
typedef void *(*enter_t)(void *data);

/**
 * leave_t - Chatroom departure notification callback type.  Once the leave
 * callback is called by motmot, the client must ensure that no further calls
 * are made into the terminating session.
 *
 * @param data      Data pointer used by the client to identify the session.
 */
typedef void (*leave_t)(void *data);

/**
 * motmot_init - Initialize libmotmot.
 *
 * This function must be called before using any of the functions below
 *
 * @param connect   Client callback for setting up connections.
 * @param chat      Client callback invoked when a chat is received.
 * @param join      Client callback invoked when a user joins the chat.
 * @param part      Client callback invoked when a user parts the chat.
 * @param enter     Client callback invoked when a chat session is joined.
 * @param leave     Client callback invoked when a chat session is left.
 * @return          0 on success, nonzero on error.
 */
int motmot_init(connect_t connect, learn_t chat, learn_t join, learn_t part,
    enter_t enter, leave_t leave);

/**
 * motmot_session - Start a new motmot chat.
 *
 * @param alias     String handle identifying the chat initiator.
 * @param size      Length of the alias.
 * @param data      Data pointer used by the client to identify the session.
 * @returns         Pointer to motmot's internal session data.  This object
 *                  should be treated as opaque by the client and should be
 *                  passed as an argument to relevant motmot functions.
 */
void *motmot_session(const char *alias, size_t size, void *data);

/**
 * motmot_watch - Watch a given channel for activity.
 *
 * @param channel   The channel for motmot to watch.
 * @returns         0 on success, nonzero on error.
 */
int motmot_watch(GIOChannel *channel);

/**
 * motmot_invite - Add user to chat.
 *
 * @param alias     String handle recognized by the client's connect callback,
 *                  used to uniquely identify the invitee.
 * @param size      Length of the alias.
 * @param data      Data pointer used by motmot to identify the session.
 * @returns         0 on success, nonzero on error.
 */
int motmot_invite(const char *alias, size_t size, void *data);

/**
 * motmot_disconnect - Request to disconnect from a chat.
 *
 * @param data      Data pointer used by motmot to identify the session.
 * @returns         0 on success, nonzero on error.
 */
int motmot_disconnect(void *data);

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 *
 * @param message   The message to be sent.
 * @param len       The length of that message.
 * @param data      Data pointer used by motmot to identify the session.
 * @returns         0 on success, nonzero on error.
 */
int motmot_send(const char *message, size_t len, void *data);

#endif // __MOTMOT_H__
