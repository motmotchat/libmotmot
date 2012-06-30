/**
 * session_local.h - Session local datatypes for Paxos.
 */
#ifndef __PAXOS_TYPES_SESSION_LOCAL_H__
#define __PAXOS_TYPES_SESSION_LOCAL_H__

#include "paxos_msgpack.h"

#include "containers/list_factory.h"
#include "types/primitives.h"
#include "types/core.h"

/* A Paxos protocol participant. */
struct paxos_acceptor {
  paxid_t pa_paxid;                   // instance number of the agent's JOIN
  struct paxos_connect *pa_conn;      // connection to acceptor
  LIST_ENTRY(paxos_acceptor) pa_le;   // sorted linked list of all participants
  // TODO: remove
  struct paxos_peer *pa_peer;
  size_t pa_size;
  void *pa_desc;
};

LIST_DECLARE(acceptor, paxid_t);
void acceptor_destroy(struct paxos_acceptor *);

/* An instance of the "synod" algorithm. */
struct paxos_instance {
  struct paxos_header pi_hdr;         // Paxos header identifying the instance
  bool pi_committed;                  // true if a commit has been received
  bool pi_cached;                     // true if the request is cached; not sent
  bool pi_learned;                    // true if learned; not sent
  unsigned pi_votes;                  // number of accepts; not sent
  unsigned pi_rejects;                // number of rejects; not sent
  LIST_ENTRY(paxos_instance) pi_le;   // sorted linked list of instances
  struct paxos_value pi_val;          // value of the decree
};

LIST_DECLARE(instance, paxid_t);
void instance_destroy(struct paxos_instance *);
void instance_init_metadata(struct paxos_instance *);

/* Request containing data, pending proposer commit. */
struct paxos_request {
  struct paxos_value pr_val;          // request ID and kind
  size_t pr_size;                     // size of data
  void *pr_data;                      // data pointer dependent on kind
  LIST_ENTRY(paxos_request) pr_le;    // sorted linked list of requests
};

LIST_DECLARE(request, reqid_t);
void request_destroy(struct paxos_request *);

/* Msgpack helpers. */
void paxos_request_pack(struct paxos_yak *, struct paxos_request *);
void paxos_request_unpack(struct paxos_request *, msgpack_object *);
void paxos_acceptor_pack(struct paxos_yak *, struct paxos_acceptor *);
void paxos_acceptor_unpack(struct paxos_acceptor *, msgpack_object *);
void paxos_instance_pack(struct paxos_yak *, struct paxos_instance *);
void paxos_instance_unpack(struct paxos_instance *, msgpack_object *);

#endif /* __PAXOS_TYPES_SESSION_LOCAL_H__ */
