/**
 * paxos_msgpack.h - Msgpack wrapper and helper functions for Paxos.
 */
#ifndef __PAXOS_MSGPACK_H__
#define __PAXOS_MSGPACK_H__

#include <msgpack.h>

/**
 * Large, wooly carrier of messages from the Himalayas.  Clients of the Paxos
 * yak should treat this struct as opaque, but we expose it to allow for
 * stack allocation.
 */
struct paxos_yak {
  msgpack_packer *pk;     // message packer
  msgpack_sbuffer *buf;   // associated read buffer; requires separate free
};

/* Paxos yak utilities. */
void paxos_payload_init(struct paxos_yak *, size_t);
void paxos_payload_begin_array(struct paxos_yak *, size_t);
void paxos_payload_destroy(struct paxos_yak *);
char *paxos_payload_data(struct paxos_yak *);
size_t paxos_payload_size(struct paxos_yak *);

#endif /* __PAXOS_MSGPACK_H__ */
