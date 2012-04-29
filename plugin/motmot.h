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
