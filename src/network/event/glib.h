/**
 * glib.h - Motmot-GLib event layer interface.
 *
 * Mostly for use in testing binaries.
 */
#ifndef __MOTMOT_EVENT_GLIB_H__
#define __MOTMOT_EVENT_GLIB_H__

#include "event/event.h"

int want_read(int, enum motmot_fdtype, void *,
    motmot_event_callback_t, void *);
int want_write(int, enum motmot_fdtype, void *,
    motmot_event_callback_t, void *);
int want_timeout(motmot_event_callback_t, void *, void *, unsigned);

#endif /* __MOTMOT_EVENT_GLIB_H__ */
