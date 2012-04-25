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
#include <msgpack.h>

#include "motmot.h"

#define err(cond, errstr)                           \
  if (cond) {                                       \
    g_error("%s: %s", errstr, strerror(errno));     \
    exit(1);                                        \
  }

GMainLoop *gmain;
GIOChannel *self_channel;

/**
 * socket_open - Create a local UNIX socket and wrap it in a GIOChannel.
 */
GIOChannel *
socket_open(const char *handle, size_t len, bool listening)
{
  int s;
  struct sockaddr_un *saddr;
  GIOChannel *channel;

  saddr = g_malloc0(sizeof(short) + len);
  saddr->sun_family = AF_UNIX;
  memcpy(&saddr->sun_path, handle, len);

  err((s = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0, "socket");

  if (listening) {
    err(bind(s, (struct sockaddr *)saddr, sizeof(short) + len) < 0, "bind");
    err(listen(s, 5) < 0, "listen");
  } else {
    if (connect(s, (struct sockaddr *)saddr, sizeof(short) + len) < 0) {
      // Connection refused.  Probably a stale socket, so let Paxos know that
      // the other client is probably dead.
      return NULL;
    }
  }

  g_free(saddr);
  channel = g_io_channel_unix_new(s);

  return channel;
}

GIOChannel *
listen_unix(const void *handle, size_t len)
{
  return socket_open((char *)handle, len, true);
}

GIOChannel *
connect_unix(const void *handle, size_t len)
{
  return socket_open((char *)handle, len, false);
}

/**
 * socket_accept - Like UNIX accept() but with GIOChannels.
 */
int
socket_accept(GIOChannel *source, GIOCondition condition, void *data)
{
  int fd, newfd;
  socklen_t len;
  struct sockaddr_un *saddr;
  GIOChannel *channel;

  fd = g_io_channel_unix_get_fd(source);
  saddr = g_new0(struct sockaddr_un, 1);

  // Spin off a new socket.
  newfd = accept(fd, (struct sockaddr *)saddr, &len);
  err(newfd < 0, "accept");

  g_free(saddr);

  // Create and watch a channel.
  channel = g_io_channel_unix_new(newfd);
  motmot_watch(channel);

  return TRUE;
}

/**
 * input_loop - Listen for input on stdin, parse, and dispatch.
 */
int
input_loop(GIOChannel *channel, GIOCondition condition, void *data)
{
  char *msg, *tmp;
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

  // Do rudimentary command line parsing.
  if (g_str_has_prefix(msg, "/invite ")) {
    // \invite socket - Handle inviting others.
    tmp = msg + 7;
    while (*++tmp == ' ');  // Move past all the spaces.
    motmot_invite(tmp, strlen(tmp));
  } else if (g_str_has_prefix(msg, "/part")) {
    // \part - Only do it if it's followed by a space or EOF.
    if (msg[6] == '\0' || msg[6] == ' ') {
      motmot_disconnect();
      g_io_channel_shutdown(self_channel, TRUE, &gerr);
      exit(0);
    }
  } else {
    // Broadcast via motmot.
    motmot_send(msg, eol + 1);
  }

  g_free(msg);
  return TRUE;
}

int
print_chat(const void *buf, size_t len) {
  printf("CHAT: %.*s\n", (int)len, (char *)buf);
  return 0;
}

int
print_join(const void *buf, size_t len) {
  printf("JOIN: %.*s\n", (int)len, (char *)buf);
  return 0;
}

int
print_part(const void *buf, size_t len) {
  printf("PART: %.*s\n", (int)len, (char *)buf);
  return 0;
}

int
main(int argc, char *argv[])
{
  int i;

  if (argc < 2) {
    printf("Usage: motmot my/sock [other/socks...]\n");
    exit(1);
  }

  // Set up the main event loop.
  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  // Create our server / listening socket.
  self_channel = listen_unix(argv[1], strlen(argv[1]));
  g_io_add_watch(self_channel, G_IO_IN, socket_accept, NULL);

  // Watch for input on stdin.
  g_io_add_watch(g_io_channel_unix_new(0), G_IO_IN, input_loop, NULL);

  // Initialize motmot.
  motmot_init(connect_unix, print_chat, print_join, print_part);

  // Start a new chat.
  if (argc > 2) {
    motmot_session(argv[1], strlen(argv[1]));
  }

  // Invite our friends!
  for (i = 2; i < argc; i++) {
    motmot_invite(argv[i], strlen(argv[i]));
  }

  g_main_loop_run(gmain);

  return 0;
}
