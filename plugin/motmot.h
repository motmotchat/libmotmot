/*
 * motmot definitions 
 */

typedef struct{
  int fd;
  char *server;
  char *port;
  PurpleAccount *account;
  PurpleSslConnection *gsc;
  const char *data;
} motmot_conn;
