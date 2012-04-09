#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib.h>

#define SOCKDIR   "conn/"
#define MAXCONNS  100

#define err(cond, errstr)   \
  if (cond) {               \
    perror(errstr);         \
    exit(1);                \
  }

GMainLoop *gmain;
struct {
  int pid;
  GIOChannel *channel;
} conns[MAXCONNS] = { 0 };

/**
 * Create a local UNIX socket and wrap it in a GIOChannel.
 *
 * All our socket files are identified by conn/xxxxxx, where xxxxxx is the
 * pid of the listening process.  Passing pid > 0 to create_socket will
 * open a connection to the corresponding file; otherwise, a listening
 * socket file will be created with the pid of the calling process.
 */
GIOChannel *
create_socket_channel(int pid)
{
  int s;
  char *filename;
  struct sockaddr_un *saddr;
  GIOChannel *channel;

  // 14 = sizeof(short) + "conn/000000".
  saddr = g_malloc0(14);
  saddr->sun_family = AF_UNIX;

  // "Six digits should be enough for a pid."
  sprintf(saddr->sun_path, SOCKDIR "%06d", (pid > 0) ? pid : getpid());

  // Open a socket.
  s = socket(PF_LOCAL, SOCK_STREAM, 0);
  err(s < 0, "socket");

  if (pid > 0) {
    // Connect to conn/pid.
    err(connect(s, (struct sockaddr *)saddr, 14) < 0, "connect");
  } else {
    // Listen on conn/pid.
    err(bind(s, (struct sockaddr *)saddr, 14) < 0, "bind");
    err(listen(s, 5) < 0, "listen");
  }
  channel = g_io_channel_unix_new(s);

  g_free(saddr);
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
  struct sockaddr_un *saddr;
  GIOChannel *channel;

  fd = g_io_channel_unix_get_fd(source);

  saddr = g_new0(struct sockaddr_un, 1);

  newfd = accept(fd, (struct sockaddr *)saddr, &len);
  err(newfd < 0, "accept");

  channel = g_io_channel_unix_new(newfd);

  // TODO: pass in some data here
  g_io_add_watch(channel, G_IO_IN, socket_recv, NULL);

  g_free(saddr);
  return TRUE;
}

int
poll_conns(void *data)
{
  int i, pid;
  bool found;
  DIR *dp;
  struct dirent *ep;
  GIOChannel *channel;

  // Open up conn/.
  dp = opendir(SOCKDIR);
  err(dp == NULL, "opendir");

  // Check all direntries.
  while (ep = readdir(dp)) {
    found = false;
    pid = atoi(ep->d_name);

    // Check whether we have already connected to the socket file.
    for (i = 0; conns[i].pid != 0; ++i) {
      if (pid == conns[i].pid) {
        found = true;
        break;
      }
    }

    // Create a new socket channel for any new processes discovered.
    if (!found) {
      conns[i].pid = pid;
      conns[i].channel = create_socket_channel(pid);
    }
  }

  closedir(dp);
  return TRUE;
}

int
main(int argc, char *argv[])
{
  GIOChannel *channel;

  // Set up the main event loop.
  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  // Create our server / listening socket.
  channel = create_socket_channel(0);
  g_io_add_watch(channel, G_IO_IN, socket_accept, NULL);

  // Poll the directory for new sockets every second.
  g_timeout_add_seconds(1, poll_conns, NULL);

  g_main_loop_run(gmain);
}
