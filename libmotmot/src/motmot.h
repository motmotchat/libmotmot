/**
 * motmot.h - the public API for libmotmot
 */

#ifndef __MOTMOT_H__
#define __MOTMOT_H__

#include <stddef.h>
#include <glib.h>

/**
 * connect_t - Connection establishment callback type.
 *
 * @param handle    An opaque handle that uniquely specifies an individual to
 *                  connect to.
 * @param size      The size of that handle
 * @returns         A GIOChannel connected to the given handle
 */
typedef GIOChannel *(*connect_t)(const void *handle, size_t size);

/**
 * learn_t - Learning callback type.
 *
 * @param message   The message to learn. For chats, this is the chat message
 *                  itself. For joins and parts, this is a opaque handle
 *                  representing the peer that has joined or parted
 * @param size      The size of that message
 * @returns         0 on success, nonzero on error
 */
typedef int (*learn_t)(const void *message, size_t size);

/**
 * motmot_init - Initialize libmotmot.
 *
 * This function must be called before using any of the functions below
 *
 * @param connect   Client callback for setting up connections
 * @param chat      Client callback invoked when a chat is received
 * @param join      Client callback invoked when a user joins the chat
 * @param part      Client callback invoked when a user parts the chat
 */
void motmot_init(connect_t connect, learn_t chat, learn_t join, learn_t part);

/**
 * motmot_session - Start a new motmot chat.
 *
 * @param desc      Identifying descriptor of the chat initiator
 * @param size      Size of the descriptor object
 */
void motmot_session(void *, const void *desc, size_t size);

/**
 * motmot_watch - Watch a given channel for activity.
 *
 * @param channel   The channel to watch
 * @returns         0 on success, nonzero on error
 */
int motmot_watch(GIOChannel *channel);

/**
 * motmot_invite - Add user to chat.
 *
 * @param handle    Object containing a handle recognized by the client's
 *                  connect callback
 * @param len       Length of the string
 * @returns         0 on success, nonzero on error
 */
int motmot_invite(const void *handle, size_t len);

/**
 * motmot_disconnect - Disconnect from a chat.
 *
 * @returns         0 on success, nonzero on error
 */
void motmot_disconnect(void);

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 *
 * @param message   The message to be sent
 * @param length    The length of that message
 * @returns         0 on success, nonzero on error
 */
int motmot_send(const char *message, size_t length);

#endif // __MOTMOT_H__
