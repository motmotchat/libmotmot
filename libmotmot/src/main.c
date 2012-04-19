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
#include <glib-unix.h>
#include <msgpack.h>

#include "motmot.h"

#define SOCKDIR   "conn/"
#define MAXCONNS  100

#define err(cond, errstr)                           \
  if (cond) {                                       \
    g_error("%s: %s", errstr, strerror(errno));     \
    exit(1);                                        \
  }

GMainLoop *gmain;

int conns[MAXCONNS];

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
  struct sockaddr_un *saddr;
  GIOChannel *channel;
  GError *gerr;

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
    if (connect(s, (struct sockaddr *)saddr, 14) < 0) {
      // Connection refused. Probably a stale socket, so just ignore it
      return NULL;
    }
  } else {
    // Listen on conn/pid.
    err(bind(s, (struct sockaddr *)saddr, 14) < 0, "bind");
    err(listen(s, 5) < 0, "listen");
  }

  gerr = NULL;
  channel = g_io_channel_unix_new(s);
  if (g_io_channel_set_encoding(channel, NULL, &gerr) == G_IO_STATUS_ERROR) {
    // TODO: error handling
    g_error("create_socket_channel: Failed to set channel encoding.\n");
  }

  g_free(saddr);
  return channel;
}

/**
 * Wrapper around create_socket_channel.
 */
GIOChannel *
create_socket_channel_motmot(const char *msg, size_t len)
{
  int pid = atoi(msg);
  if (pid != 0) {
    return create_socket_channel(pid);
  } else {
    return NULL;
  }
}

/**
 * Clean up the mess we have made.  Currently, this just involves removing our
 * listening socket in SOCKDIR.
 */
int
cleanup(void *data)
{
  // Something something avoid mallocing near signal handlers something.
  static char sockname[12];

  sprintf(sockname, SOCKDIR "%06d", getpid());
  unlink(sockname);

  // Let's get out of here.
  g_main_loop_quit(gmain);
  return FALSE;
}

int
socket_accept(GIOChannel *source, GIOCondition condition, void *data)
{
  int fd, newfd;
  socklen_t len;
  struct sockaddr_un *saddr;

  msgpack_unpacker *pac;
  GIOChannel *channel;
  GError *gerr;

  fd = g_io_channel_unix_get_fd(source);
  saddr = g_new0(struct sockaddr_un, 1);

  // Spin off a new socket.
  newfd = accept(fd, (struct sockaddr *)saddr, &len);
  err(newfd < 0, "accept");

  // Wrap it in a channel.
  channel = g_io_channel_unix_new(newfd);
  if (g_io_channel_set_encoding(channel, NULL, &gerr) == G_IO_STATUS_ERROR) {
    // TODO: error handling
    g_error("socket_accept: Failed to set channel encoding.\n");
  }
  // TODO: check for errors here
  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, &gerr);

  // Associate a msgpack unpacker and watch it.
  pac = msgpack_unpacker_new(MSGPACK_UNPACKER_INIT_BUFFER_SIZE);
  if (pac == NULL) {
    // TODO: error handling
    g_error("socket_accept: Failed to allocate msgpack_unpacker.\n");
  }
  // XXX: Paxosify this
  // g_io_add_watch(channel, G_IO_IN, socket_recv, pac);

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

  // Open up conn/.
  dp = opendir(SOCKDIR);
  err(dp == NULL, "opendir");

  // Search through all connections.
  while ((ep = readdir(dp))) {
    // Ignore . and .. and any files that shouldn't exist.
    pid = atoi(ep->d_name);
    if (pid == 0 || pid == getpid()) {
      continue;
    }

    // Check whether we have already connected to the socket.
    found = false;
    for (i = 0; conns[i] != 0; ++i) {
      if (pid == conns[i]) {
        found = true;
        break;
      }
    }

    if (!found) {
      // TODO: Connect.
    }
  }

  closedir(dp);
  return TRUE;
}

int
input_loop(GIOChannel *channel, GIOCondition condition, void *data)
{
  char *msg;
  unsigned long eol;
  GError *gerr;
  GIOStatus status;

  // Read in a line.
  status = g_io_channel_read_line(channel, &msg, NULL, &eol, &gerr);
  if (status != G_IO_STATUS_NORMAL) {
    g_error("Error reading from stdin.\n");
    return FALSE;
  }

  // Kill the trailing newline.
  msg[eol] = '\0';

  // Broadcast via motmot.
  motmot_send(msg, eol + 1);

  g_free(msg);
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

  // Let's try to clean up ourselves in the case someone kills us.
  g_unix_signal_add(SIGINT, cleanup, NULL);
  g_unix_signal_add(SIGTERM, cleanup, NULL);

  // Poll the directory for new sockets every second.
  g_timeout_add_seconds(1, poll_conns, NULL);

  // Add the input loop as an idle source.
  g_io_add_watch(g_io_channel_unix_new(0), G_IO_IN, input_loop, NULL);

  // Initialize motmot.
  motmot_init(create_socket_channel_motmot);

  g_main_loop_run(gmain);

  return 0;
}
