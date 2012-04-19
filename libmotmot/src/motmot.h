/**
 * motmot.h - the public API for libmotmot
 */

#ifndef __MOTMOT_H__
#define __MOTMOT_H__

#include <stddef.h>
#include <glib.h>

#include "paxos.h"

typedef int (*motmot_callback)(const char *, size_t, void *);

/**
 * motmot_init - Initialize libmotmot
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
 * motmot_send - Queue the message for reliable ordered broadcast
 *
 * @param message   The message to be sent
 * @param length    The length of that message
 * @returns         0 on success, nonzero on error
 */
int motmot_send(const char *message, size_t length);

/**
 * motmot_add_callback - Add a callback that will be called upon the receipt of
 * every message.
 *
 * @param fn        A motmot_callback function pointer that will be invoked
 *                  whenever a paxos message is received
 * @param data      A pointer that will be passed to every invocation of fn
 * @returns         0 on success, nonzero on error
 */
int motmot_add_callback(motmot_callback fn, void *data);

/**
 * motmot_remove_callback - Remove the given callback
 *
 * @param fn        A motmot_callback function pointer to remove
 * @param data      The same pointer passed to motmot_add_callback
 * @returns         0 if the callback was removed, 1 if it was not found
 */
int motmot_remove_callback(motmot_callback fn, void *data);

#endif // __MOTMOT_H__
