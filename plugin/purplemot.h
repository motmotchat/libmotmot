/**
 * motmot definitions
 */

#ifndef __PURPLEMOT_H__
#define __PURPLEMOT_H__

#include "prpl.h"

struct pm_account {
  // libpurple objects
  PurpleAccount *pa;            // libpurple calls this 'account', but I want
                                // to use that name for other things
  PurpleSslConnection *gsc;     // Name is libpurple's convention, not mine

  // Server connection info
  const char *server_host;
  msgpack_unpacker unpacker;    // A msgpack unpacker (includes a buffer)
};

struct pm_buddy {
  struct pm_account *account;

  // How do we contact them?
  const char *ip;
  int port;

  // How do we identify this buddy to libmotmot?
  void *lm_data;
};

struct pm_conversation {
  struct pm_account *account;
  PurpleConversation *convo;
  // TODO(carl): finish this;
};

#endif
