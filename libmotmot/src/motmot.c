/**
 * motmot.c - libmotmot API
 */

#include <glib.h>

#include "motmot.h"
#include "paxos.h"

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

  return paxos_init(connect, &learn, enter, leave);
}

/**
 * motmot_session - Start a new motmot chat.
 */
void *
motmot_session(const char *alias, size_t size, void *data)
{
  return paxos_start(alias, size, data);
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
motmot_invite(const char *alias, size_t len, void *data)
{
  return paxos_request(data, DEC_JOIN, alias, len);
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
