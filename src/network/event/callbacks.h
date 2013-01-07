/**
 * event_callbacks.h - Event layer callbacks.
 */
#ifndef __MOTMOT_EVENT_CALLBACKS_H__
#define __MOTMOT_EVENT_CALLBACKS_H__

#include "event/event.h"

extern motmot_want_io_callback_t      motmot_event_want_read;
extern motmot_want_io_callback_t      motmot_event_want_write;
extern motmot_want_timeout_callback_t motmot_event_want_timeout;

#endif /* __MOTMOT_EVENT_CALLBACKS_H__ */
