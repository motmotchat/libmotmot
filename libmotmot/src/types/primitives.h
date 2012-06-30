/**
 * primitives.h - Paxos primitive types.
 */
#ifndef __PAXOS_TYPES_PRIMITIVES_H__
#define __PAXOS_TYPES_PRIMITIVES_H__

#include <stdbool.h>
#include <stdint.h>

#include "paxos_msgpack.h"

/* Paxos ID and UUID types. */
typedef uint32_t  paxid_t;
typedef uint64_t  pax_uuid_t;

/* Totally ordered pair of paxid's. */
typedef struct paxid_pair {
  paxid_t id;             // ID of participant
  paxid_t gen;            // generation number of some sort
} ppair_t;

/* A string!  Used in Paxos! */
typedef struct paxos_string {
  size_t size;
  const char *data;
} pax_str_t;

/* Comparison functions. */
int paxid_compare(paxid_t, paxid_t);
int ppair_compare(ppair_t, ppair_t);
int pax_str_compare(pax_str_t *, pax_str_t *);

/* UUID helpers. */
void pax_uuid_gen(pax_uuid_t *);
void pax_uuid_destroy(pax_uuid_t *);
int pax_uuid_compare(pax_uuid_t *, pax_uuid_t *);

/* Artificial msgpack primitives. */
void msgpack_pack_paxid(msgpack_packer *, paxid_t);
void msgpack_pack_pax_uuid(msgpack_packer *, pax_uuid_t);

/* Msgpack helpers. */
void paxos_paxid_pack(struct paxos_yak *, paxid_t);
void paxos_paxid_unpack(paxid_t *, msgpack_object *);
void paxos_uuid_pack(struct paxos_yak *, pax_uuid_t *);
void paxos_uuid_unpack(pax_uuid_t *, msgpack_object *);

#endif /* __PAXOS_TYPES_PRIMITIVES_H__ */
