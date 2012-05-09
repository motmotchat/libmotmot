/**
 * motmot.c - libmotmot API
 */

#include "motmot.h"
#include "paxos.h"
#include "list.h"

#include <glib.h>

/**
 * motmot_init - Initialize libmotmot.
 */
int
motmot_init(connect_t connect, learn_t chat, learn_t join, learn_t part,
    enter_t enter, leave_t leave)
{
  struct learn_table learn;

  learn.chat = chat;
  learn.join = join;
  learn.part = part;

  g_timeout_add_seconds(1, paxos_sync, NULL);
  return paxos_init(connect, &learn, enter, leave);
}

/**
 * motmot_session - Start a new motmot chat.
 */
void *
motmot_session(const void *desc, size_t size, void *data)
{
  return paxos_start(desc, size, data);
}

/**
 * motmot_watch - Watch a given channel for activity.
 */
int
motmot_watch(GIOChannel *channel)
{
  return paxos_register_connection(channel);
}

/**
 * motmot_invite - Add user to chat.
 */
int
motmot_invite(const void *handle, size_t len, void *data)
{
  return paxos_request(data, DEC_JOIN, handle, len);
}

/**
 * motmot_disconnect - Request to disconnect from a chat.
 */
int
motmot_disconnect(void *data)
{
  return paxos_end(data);
}

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 */
int
motmot_send(const char *message, size_t len, void *data)
{
  return paxos_request(data, DEC_CHAT, message, len);
}
