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
}

/**
 * motmot_session - Start a new motmot chat.
 */
void
motmot_session()
{
  paxos_start();
}

/**
 * motmot_invite - Add user to chat.
 */
int
motmot_invite(char *handle, size_t len)
{
  return paxos_request(DEC_JOIN, handle, len);
}

/**
 * motmot_disconnect - Disconnect from a chat.
 */
int
motmot_disconnect()
{
  return paxos_request(DEC_PART, NULL, 0);
}

/**
 * motmot_send - Queue the message for reliable ordered broadcast.
 */
int
motmot_send(const char *message, size_t len)
{
  return paxos_request(DEC_CHAT, message, len);
}
