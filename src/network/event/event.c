/**
 * event.c - Motmot event loop abstraction layer.
 */

#include <assert.h>

#include "event.h"

motmot_want_io_callback_t       motmot_event_want_read;
motmot_want_io_callback_t       motmot_event_want_write;
motmot_want_timeout_callback_t  motmot_event_want_timeout;

static int did_init = 0;

int motmot_event_init(motmot_want_io_callback_t want_read,
    motmot_want_io_callback_t want_write,
    motmot_want_timeout_callback_t want_timeout)
{
  assert(!did_init);

  motmot_event_want_read    = want_read;
  motmot_event_want_write   = want_write;
  motmot_event_want_timeout = want_timeout;

  did_init = 1;

  return 0;
}

int motmot_event_did_init()
{
  return did_init;
}
