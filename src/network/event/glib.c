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
};

static gboolean
call_callback(void *data)
{
  int r;
  struct callback *cb = data;

  r = cb->func(cb->arg);
  if (!r) { free(cb); }

  return r;
}

static gboolean
socket_ready(GIOChannel *source, GIOCondition cond, void *data)
{
  return call_callback(data);
}

static struct callback *
callback_new(motmot_event_callback_t func, void *arg)
{
  struct callback *cb;

  cb = malloc(sizeof(*cb));
  assert(cb != NULL);
  cb->func = func;
  cb->arg = arg;

  return cb;
}

int
want_read(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  g_io_add_watch((GIOChannel *)data, G_IO_IN, socket_ready,
      callback_new(func, arg));

  return 0;
}

int
want_write(int fd, enum motmot_fdtype fdtype, void *data,
    motmot_event_callback_t func, void *arg)
{
  g_io_add_watch((GIOChannel *)data, G_IO_OUT, socket_ready,
      callback_new(func, arg));

  return 0;
}

int
want_timeout(motmot_event_callback_t func, void *arg, void *data, unsigned msecs)
{
  g_timeout_add(msecs, call_callback, callback_new(func, arg));

  return 0;
}
