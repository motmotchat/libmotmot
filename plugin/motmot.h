/*
 * motmot definitions 
 */

typedef struct{
  int fd;
  char *server;
  char *port;
  PurpleAccount *account;
  PurpleSslConnection *gsc;
} motmot_conn;
