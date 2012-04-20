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

/**
 * Create a local UNIX socket and wrap it in a GIOChannel.
 */
GIOChannel *
open_socket(const char *handle, size_t len, bool listening)
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
      // Connection refused. Probably a stale socket, so just ignore it
      g_warning("Connection refused");
      return NULL;
    }
  }

  g_free(saddr);

  channel = g_io_channel_unix_new(s);
  if (g_io_channel_set_encoding(channel, NULL, NULL) == G_IO_STATUS_ERROR) {
    g_error("socket_to_gio: Failed to set channel encoding.\n");
    return NULL;
  }

  return channel;
}

GIOChannel *
listen_unix(const char *handle, size_t len)
{
  return open_socket(handle, len, true);
}

/**
 * Wrapper around create_socket_channel.
 */
GIOChannel *
connect_unix(const char *handle, size_t len)
{
  return open_socket(handle, len, false);
}

int
socket_accept(GIOChannel *source, GIOCondition condition, void *data)
{
  int fd, newfd;
  socklen_t len;
  struct sockaddr_un *saddr;

  GIOChannel *channel;
  GError *gerr;

  fd = g_io_channel_unix_get_fd(source);
  saddr = g_new0(struct sockaddr_un, 1);

  // Spin off a new socket.
  newfd = accept(fd, (struct sockaddr *)saddr, &len);
  err(newfd < 0, "accept");

  g_free(saddr);

  // Wrap it in a channel.
  channel = g_io_channel_unix_new(newfd);
  if (g_io_channel_set_encoding(channel, NULL, &gerr) == G_IO_STATUS_ERROR) {
    // TODO: error handling
    g_error("socket_accept: Failed to set channel encoding.\n");
  }
  // TODO: check for errors here
  g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, &gerr);

  motmot_watch(channel);

  return TRUE;
}

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
    // \invite socket - Handle inviting others
    tmp = msg + 7;
    while (*++tmp == ' '); // Move past all the spaces.
    motmot_invite(tmp, strlen(tmp));
  } else if (g_str_has_prefix(msg, "/part")) {
    // \part - Only do it if it's followed by a space or EOF
    if (msg[6] == '\0' || msg[6] == ' ') {
      motmot_disconnect();
    }
  } else {
    // Broadcast via motmot.
    motmot_send(msg, eol + 1);
  }

  g_free(msg);
  return TRUE;
}

int
print_chat(const char *buf, size_t len) {
  printf("CHAT: %.*s\n", (int)len, buf);
  return 0;
}

int
print_join(const char *buf, size_t len) {
  printf("JOIN: %.*s\n", (int)len, buf);
  return 0;
}

int
print_part(const char *buf, size_t len) {
  printf("PART: %.*s\n", (int)len, buf);
  return 0;
}

int
main(int argc, char *argv[])
{
  int i;
  GIOChannel *channel;

  if (argc < 2) {
    printf("Usage: motmot my/sock [other/socks...]\n");
    exit(1);
  }

  // Set up the main event loop.
  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  // Create our server / listening socket.
  channel = listen_unix(argv[1], strlen(argv[1]));
  g_io_add_watch(channel, G_IO_IN, socket_accept, NULL);

  // Watch for input on stdin.
  g_io_add_watch(g_io_channel_unix_new(0), G_IO_IN, input_loop, NULL);

  // Initialize motmot.
  motmot_init(connect_unix, print_chat, print_join, print_part);

  // Start a new chat.
  if (argc > 2) {
    motmot_session(argv[1], strlen(argv[1]));
  }

  for (i = 2; i < argc; i++) {
    motmot_invite(argv[i], strlen(argv[i]));
  }

  g_main_loop_run(gmain);

  return 0;
}
