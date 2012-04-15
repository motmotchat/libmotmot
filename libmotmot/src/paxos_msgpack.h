/**
 * paxos_msgpack.h - Msgpack helpers for Paxos.
 */
#ifndef __PAXOS_MSGPACK_H__
#define __PAXOS_MSGPACK_H__

#include "paxos.h"
#include <msgpack.h>

struct paxos_yak {
  msgpack_packer *pk;
  msgpack_sbuffer *buf;
};

void paxos_payload_new(struct paxos_yak *, size_t);
void paxos_payload_begin_array(struct paxos_yak *, size_t);
void paxos_payload_destroy(struct paxos_yak *);
char *paxos_payload_data(struct paxos_yak *);
size_t paxos_payload_size(struct paxos_yak *);

void paxos_hdr_pack(struct paxos_yak *, struct paxos_hdr *);
void paxos_hdr_unpack(struct paxos_hdr *, msgpack_object *);
void paxos_val_pack(struct paxos_yak *, struct paxos_val *);
void paxos_val_unpack(struct paxos_val *, msgpack_object *);

#endif /* __PAXOS_MSGPACK_H__ */
