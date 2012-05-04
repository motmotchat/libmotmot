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

/*
  Seems as good a place as any to put this (for my benefit as much as anyone else's)

  General overview of control flow for motmot functions:

  To send a message, purplemot_chat_send calls motmot_send, which handles paxos.
  Once paxos is handled, print_chat_motmot calls receive_chat_message.

  To join a chat, purplemot_join_chat calls motmot_invite, which handles paxos.
  Once paxos is handled, print_join_motmot tells everyone that someone joined.

  To leave a chat, purplemot_chat_leave calls motmot_disconnect, which handles paxos.
  Once paxos is handled, print_part_motmot calls left_chat_room to tell everyone who left.
*/