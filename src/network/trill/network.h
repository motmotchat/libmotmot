#ifndef __TRILL_NETWORK_H__
#define __TRILL_NETWORK_H__

#include <stdint.h>
#include <netinet/in.h>

#include "network/trill/common.h"

/**
 * These were chosen to be non-colliding with TLS ContentTypes
 * http://tools.ietf.org/html/rfc2246#appendix-A.1
 */
enum trill_net_message_types {
  TRILL_NET_ACK = 99,
  TRILL_NET_NOACK = 100
};

// TODO: documentation
int trill_connection_probe(struct trill_connection *conn);
int trill_connection_read_probe(struct trill_connection *conn);

#endif // __TRILL_NETWORK_H__
