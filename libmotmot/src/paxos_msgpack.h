/**
 * paxos_msgpack.h - Msgpack helpers for Paxos.
 */
#ifndef __PAXOS_MSGPACK_H__
#define __PAXOS_MSGPACK_H__

#include "paxos.h"
#include <msgpack.h>

// We often need a (char *, size_t) tuple (in argument lists, for instance)
#define UNYAK(yak) paxos_payload_data(yak), paxos_payload_size(yak)

struct paxos_yak {
  msgpack_packer *pk;
  msgpack_sbuffer *buf;
};

void paxos_payload_init(struct paxos_yak *, size_t);
void paxos_payload_begin_array(struct paxos_yak *, size_t);
void paxos_payload_destroy(struct paxos_yak *);
char *paxos_payload_data(struct paxos_yak *);
size_t paxos_payload_size(struct paxos_yak *);

void paxos_header_pack(struct paxos_yak *, struct paxos_header *);
void paxos_header_unpack(struct paxos_header *, msgpack_object *);
void paxos_value_pack(struct paxos_yak *, struct paxos_value *);
void paxos_value_unpack(struct paxos_value *, msgpack_object *);

#endif /* __PAXOS_MSGPACK_H__ */
