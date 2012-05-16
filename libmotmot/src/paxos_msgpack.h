/**
 * paxos_msgpack.h - Msgpack helpers for Paxos.
 */
#ifndef __PAXOS_MSGPACK_H__
#define __PAXOS_MSGPACK_H__

#include <msgpack.h>

#include "paxos.h"

// We often need a (char *, size_t) tuple (in argument lists, for instance).
#define UNYAK(yak) paxos_payload_data(yak), paxos_payload_size(yak)

// Clients of paxos_msgpack.h should treat this struct as opaque.
struct paxos_yak {
  msgpack_packer *pk;     // message packer
  msgpack_sbuffer *buf;   // associated read buffer; requires separate free
};

void paxos_payload_init(struct paxos_yak *, size_t);
void paxos_payload_begin_array(struct paxos_yak *, size_t);
void paxos_payload_destroy(struct paxos_yak *);
char *paxos_payload_data(struct paxos_yak *);
size_t paxos_payload_size(struct paxos_yak *);

void paxos_paxid_pack(struct paxos_yak *, paxid_t);
void paxos_paxid_unpack(paxid_t *, msgpack_object *);
void paxos_uuid_pack(struct paxos_yak *, pax_uuid_t);
void paxos_uuid_unpack(pax_uuid_t *, msgpack_object *);

void paxos_header_pack(struct paxos_yak *, struct paxos_header *);
void paxos_header_unpack(struct paxos_header *, msgpack_object *);
void paxos_value_pack(struct paxos_yak *, struct paxos_value *);
void paxos_value_unpack(struct paxos_value *, msgpack_object *);
void paxos_request_pack(struct paxos_yak *, struct paxos_request *);
void paxos_request_unpack(struct paxos_request *, msgpack_object *);

void paxos_acceptor_pack(struct paxos_yak *, struct paxos_acceptor *);
void paxos_acceptor_unpack(struct paxos_acceptor *, msgpack_object *);
void paxos_instance_pack(struct paxos_yak *, struct paxos_instance *);
void paxos_instance_unpack(struct paxos_instance *, msgpack_object *);

#endif /* __PAXOS_MSGPACK_H__ */
