/**
 * event.h - Motmot event loop abstraction layer.
 *
 * This layer of indirection aims to make Motmot's networking library (Trill
 * and Plume) event loop agnostic.
 *
 * The event loop or application should implement the callbacks required by
 * motmot_event_init to provide the library with access to the event loop.
 * The library will call these functions to request event notifications,
 * providing a callback to be invoked when the requested event occurs.
 */
#ifndef __MOTMOT_EVENT_H__
#define __MOTMOT_EVENT_H__

/**
 * enum motmot_fdtype - Types of file descriptors for use by the event loop.
 */
enum motmot_fdtype {
  MOTMOT_EVENT_TCP,
  MOTMOT_EVENT_UDP,
  MOTMOT_EVENT_FILE,
};

/**
 * motmot_event_callback_t - Callback from the event loop or application to the
 * library.  These callbacks are provided when event notification is requested,
 * and should be invoked when the desired event occurs.
 *
 * @param arg       Opaque library data that is passed as part of the request.
 * @return          0 to cancel notification, 1 to continue or to reschedule.
 */
typedef int (*motmot_event_callback_t)(void *arg);

/**
 * motmot_want_io_callback_t - Callback from the library to the event loop.
 * Indicates that the library wishes to receive can-read or can-write events.
 *
 * @param fd        The file descriptor for which the library wants can-write
 *                  events.
 * @param fdtype    The type of the file descriptor.
 * @param data      Event loop or application data that has been associated
 *                  with the file descriptor through library calls.
 * @param func      The callback to invoke when the event occurs.
 * @param arg       Library data to pass to the callback when the event occurs.
 * @return          0 on success, nonzero on error.
 */
typedef int (*motmot_want_io_callback_t)(int fd, enum motmot_fdtype fdtype,
    void *data, motmot_event_callback_t func, void *arg);

/**
 * motmot_want_timeout_callback_t - Callback from the library to the event
 * loop.  Indicates that the library wishes to receive timeout events.  These
 * should fire at regular intervals until `func' returns 0.
 *
 * @param func      The callback to invoke when the timeout triggers.  The
 *                  timer should be cancelled when 0 is returned.
 * @param arg       Library data to pass to the callback when the event occurs.
 * @param data      Event loop or application data that has been associated
 *                  with the `arg' parameter through library calls.
 * @param msecs     The number of milliseconds desired between calls.
 * @return          0 on success, nonzero on error.
 */
typedef int (*motmot_want_timeout_callback_t)(motmot_event_callback_t func,
    void *arg, void *data, unsigned int msecs);

/**
 * motmot_event_init - Initialize the Motmot event layer.
 *
 * This function should be called exactly once, before any library functions
 * are called.
 *
 * @param want_read     Callback to request can-read event notifications.
 * @param want_write    Callback to request can-write event notifications.
 * @param want_timeout  Callback to request timeout event notifications.
 */
int motmot_event_init(motmot_want_io_callback_t want_read,
    motmot_want_io_callback_t want_write,
    motmot_want_timeout_callback_t want_timeout);

#endif /* __MOTMOT_EVENT_H__ */
