#ifndef __TRILL_NETWORK_H__
#define __TRILL_NETWORK_H__

/**
 * Probe message types, chosen to be non-colliding with TLS
 * ContentTypes http://tools.ietf.org/html/rfc2246#appendix-A.1.
 */
#define TRILL_NET_ACK   99
#define TRILL_NET_NOACK 100

// TODO: documentation
int trill_connection_probe(void *);
int trill_connection_read_probe(struct trill_connection *);

#endif // __TRILL_NETWORK_H__
