#ifndef __TRILL_H__
#define __TRILL_H__

#include <stdint.h>
#include <sys/types.h>

struct trill_connection;

typedef int (*trill_callback_t)(struct trill_connection *conn);
typedef int (*trill_want_write_callback_t)(void *data);
typedef int (*trill_want_timeout_callback_t)(void *data,
    trill_callback_t callback, unsigned int millis);

typedef ssize_t (*trill_recv_callback_t)(void *data, size_t len, uint64_t *seq);

/**
 * Various event loop callbacks.
 */
struct trill_callback_vtable {
  trill_want_write_callback_t want_write_callback;
  trill_want_timeout_callback_t want_timeout_callback;
};

/**
 * Initialize Trill. Should be called exactly once before any other trill
 * functions are called.
 */
int trill_init(const struct trill_callback_vtable *vtable);

struct trill_connection *trill_connection_new();
int trill_connection_free(struct trill_connection *conn);

int trill_get_fd(const struct trill_connection *conn);
uint16_t trill_get_port(const struct trill_connection *conn);

int trill_set_key(struct trill_connection *conn, const char *key_pem,
    size_t key_len, const char *cert_pem, size_t cert_len);

int trill_set_ca(struct trill_connection *conn, const char *ca_pem,
    size_t ca_len);

int trill_connect(struct trill_connection *conn, const char *address,
    uint16_t port);

void trill_set_data(struct trill_connection *conn, void *data);

int trill_can_read(struct trill_connection *conn);

int trill_can_write(struct trill_connection *conn);

void trill_set_recv_callback(struct trill_connection *conn,
    trill_recv_callback_t callback);

ssize_t trill_send(struct trill_connection *conn, const void *data, size_t len);

#endif // __TRILL_H__
