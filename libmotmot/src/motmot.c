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
void
motmot_init(connect_t connect, learn_t chat, learn_t join, learn_t part)
{
  struct learn_table learn;

  learn.chat = chat;
  learn.join = join;
  learn.part = part;

  paxos_init(connect, &learn);
  g_timeout_add_seconds(1, paxos_sync, NULL);
}

/**
 * motmot_session - Start a new motmot chat.
 */
void
motmot_session(void *id, const void *desc, size_t size)
{
  paxos_start(desc, size);
}

/**
 * motmot_watch - Watch a given channel for activity.
 */
int
motmot_watch(GIOChannel *channel)
{
  paxos_register_connection(channel);
  return 0;
}

/**
 * motmot_invite - Add user to chat.
 */
int
motmot_invite(const void *handle, size_t len)
{
  return paxos_request(DEC_JOIN, handle, len);
}

/**
 * motmot_disconnect - Disconnect from a chat.
 */
void
motmot_disconnect()
{
  paxos_end();
}

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 */
int
motmot_send(const char *message, size_t len)
{
  return paxos_request(DEC_CHAT, message, len);
}
