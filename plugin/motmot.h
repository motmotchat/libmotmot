/*
 * motmot definitions 
 */

typedef struct{
  GIOChannel *fd;
  char *server;
  char *port;
  PurpleAccount *account;
} motmot_conn;
