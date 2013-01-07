/**
 * glib.c - GLib event layer implementation.
 */

#include <assert.h>
#include <stdlib.h>
#include <glib.h>

#include "event/glib.h"

struct callback {
  motmot_event_callback_t func;
  void *arg;
  GIOChannel *channel;
  int ephemeral;
};

static gboolean
call_callback(void *data)
{
  int r;
  struct callback *cb = data;

  r = cb->func(cb->arg);
  if (!r) {
    if (cb->channel && cb->ephemeral) {
      g_io_channel_unref(cb->channel);
    }
    free(cb);
  }

  return r;
}

static gboolean
socket_ready(GIOChannel *source, GIOCondition cond, void *data)
{
  return call_callback(data);
}

static struct callback *
callback_new(motmot_event_callback_t func, void *arg, GIOChannel *channel,
    int ephemeral)
{
  struct callback *cb;

  cb = malloc(sizeof(*cb));
  assert(cb != NULL);

  cb->func = func;
  cb->arg = arg;
  cb->channel = channel;
  cb->ephemeral = ephemeral;

  return cb;
}

int
want_read(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  GIOChannel *channel;
  struct callback *cb;
  int ephemeral = 0;

  channel = (GIOChannel *)data;

  if (channel == NULL) {
    channel = g_io_channel_unix_new(fd);
    ephemeral = 1;
  }

  cb = callback_new(func, arg, channel, ephemeral);
  g_io_add_watch(channel, G_IO_IN, socket_ready, cb);

  return 0;
}

int
want_write(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  GIOChannel *channel;
  struct callback *cb;
  int ephemeral = 0;

  channel = (GIOChannel *)data;

  if (channel == NULL) {
    channel = g_io_channel_unix_new(fd);
    ephemeral = 1;
  }

  cb = callback_new(func, arg, channel, ephemeral);
  g_io_add_watch(channel, G_IO_OUT, socket_ready, cb);

  return 0;
}

int
want_timeout(motmot_event_callback_t func, void *arg, void *data, unsigned msecs)
{
  g_timeout_add(msecs, call_callback, callback_new(func, arg, NULL, 0));

  return 0;
}

void
motmot_event_glib_init()
{
  motmot_event_init(want_read, want_write, want_timeout);
}
