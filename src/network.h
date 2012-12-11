#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <stdint.h>
#include <netinet/in.h>

struct trill_connection {
  int tc_sock_fd;                 // A bound listening UDP socket
  uint16_t tc_port;               // The local UDP port we're listening on
  struct sockaddr_in tc_remote;   // The remote address we're connected to
};

/**
 * Allocate and initialize a new listening trill connection.
 *
 * Returns NULL on error, or a pointer to a trill_connection struct with a
 * populated file descriptor
 */
struct trill_connection *trill_connection_new();

/**
 * "Connect" to a new remote. This really just initializes the tc_remote field
 * in the trill_connection struct -- no data is actually sent.
 */
int trill_connection_connect(struct trill_connection *conn, const char *addr,
    uint16_t port);

/**
 * Free a trill connection. Also closes the associated socket.
 */
int trill_connection_free(struct trill_connection *conn);

#endif // __NETWORK_H__
