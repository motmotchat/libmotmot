#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define err(cond, errstr) \
  if (cond) { \
    perror(errstr); \
    exit(1); \
  }

GMainLoop *gmain;

GIOChannel *
open_listening_socket()
{
  int s;
  char *filename;
  struct sockaddr_un *local;
  GIOChannel *channel;

  // 14 = sizeof(short) + "conn/000000"
  local = g_malloc0(14);
  local->sun_family = AF_UNIX;

  // "Six digits should be enough for a pid"
  sprintf((char *)local + sizeof(short), "conn/%06d", getpid());

  // Go through the socket opening song and dance
  err((s = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1, "Unable to open socket");
  err(bind(s, (struct sockaddr *)local, 14), "Unable to bind socket");
  err(listen(s, 5) == -1, "Unable to listen on socket");

  channel = g_io_channel_unix_new(s);

  g_free(local);
  return channel;
}

int
socket_recv(GIOChannel *source, GIOCondition condition, void *data)
{
  return TRUE;
}

int
socket_accept(GIOChannel *source, GIOCondition condition, void *data)
{
  int fd, newfd, len;
  struct sockaddr_un *socket;
  GIOChannel *channel;

  socket = g_new0(struct sockaddr_un, 1);

  fd = g_io_channel_unix_get_fd(source);

  newfd = accept(fd, (struct sockaddr *)socket, &len);
  err(newfd == -1, "Error accepting a new connection");

  channel = g_io_channel_unix_new(newfd);

  // TODO: pass in some data here
  g_io_add_watch(channel, G_IO_IN, socket_recv, NULL);

  return TRUE;
}

int
main(int argc, char *argv[])
{
  GIOChannel *channel;

  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  channel = open_listening_socket();

  g_io_add_watch(channel, G_IO_IN, socket_accept, NULL);

  g_main_loop_run(gmain);
}
