/**
 * motmot.h - the public API for libmotmot
 */

#ifndef __MOTMOT_H__
#define __MOTMOT_H__

#include <stddef.h>
#include <glib.h>

#include "paxos.h"

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
 * motmot_invite - Add user to chat.
 *
 * @param handle    String containing a handle recognized by the client's
 *                  connect callback
 * @param len       Length of the string
 * @returns         0 on success, nonzero on error
 */
int motmot_invite(char *handle, size_t len);

/**
 * motmot_disconnect - Disconnect from a chat.
 *
 * @returns         0 on success, nonzero on error
 */
int
motmot_disconnect(void);

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 *
 * @param message   The message to be sent
 * @param length    The length of that message
 * @returns         0 on success, nonzero on error
 */
int motmot_send(const char *message, size_t length);

#endif // __MOTMOT_H__
