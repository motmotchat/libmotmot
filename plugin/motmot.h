/*
 * motmot definitions 
 */

typedef struct{
  int fd;
  char *server;
  char *port;
  PurpleAccount *account;
  PurpleSslConnection *gsc;
  GList *acceptance_list;
  void *data;
} motmot_conn;

typedef struct{
  const char *addr;
  int port;
} motmot_buddy;

// JULIE
// struct for passing data around to motmot callbacks
typedef struct {
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
} MotmotInfo;
