/**
 * motmot definitions
 */

#ifndef __PURPLEMOT_H__
#define __PURPLEMOT_H__

#include "prpl.h"

// TODO: documentation
struct motmot_conn {
  int fd;
  char *server;
  char *port;
  PurpleAccount *account;
  PurpleSslConnection *gsc;
  GList *acceptance_list;
  void *data;
  GList *info_list;
};

// TODO: documentation
struct motmot_buddy {
  const char *addr;
  int port;
};

// Struct for passing data around to motmot callbacks.
// TODO: documentation
struct MotmotInfo {
  PurpleAccount *account;
  PurpleBuddy *buddy;
  PurpleConnection *connection;
  int id;
  const char *message;
  PurpleMessageFlags flags;
  //GHashTable components;
  PurpleConversation *from;
  PurpleConversation *to;
  gpointer room;
  GHashTable *components;
  void *internal_data;
};

#endif
