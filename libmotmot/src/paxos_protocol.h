/**
 * paxos_protocol.h - Prototypes for protocol internal functions.
 */
#ifndef __PAXOS_PROTOCOL_H__
#define __PAXOS_PROTOCOL_H__

#include "paxos.h"

#include <glib.h>
#include <msgpack.h>

/* Learner operations. */
int paxos_commit(struct paxos_instance *);
int paxos_learn(struct paxos_instance *, struct paxos_request *);

/* Proposer operations. */
int proposer_prepare(void);
int proposer_ack_promise(struct paxos_header *, msgpack_object *);
int proposer_decree(struct paxos_instance *);
int proposer_ack_accept(struct paxos_peer *, struct paxos_header *);
int proposer_commit(struct paxos_instance *);

/* Acceptor operations. */
int acceptor_ack_prepare(struct paxos_peer *, struct paxos_header *);
int acceptor_promise(struct paxos_header *);
int acceptor_ack_decree(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_accept(struct paxos_header *);
int acceptor_ack_commit(struct paxos_header *);

/* Participant initiation protocol. */
int proposer_welcome(struct paxos_acceptor *);
int acceptor_ack_welcome(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_ptmy(struct paxos_acceptor *);
int proposer_ack_ptmy(struct paxos_header *);
int proposer_greet(struct paxos_header *, struct paxos_acceptor *acc);
int acceptor_ack_greet(struct paxos_header *);
int acceptor_hello(struct paxos_acceptor *);
int acceptor_ack_hello(struct paxos_peer *, struct paxos_header *);

/* Out-of-band request protocol. */
int proposer_ack_request(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_ack_request(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int paxos_retrieve(struct paxos_instance *);
int paxos_ack_retrieve(struct paxos_header *, msgpack_object *);
int paxos_resend(struct paxos_acceptor *, struct paxos_header *,
    struct paxos_request *);
int paxos_ack_resend(struct paxos_header *, msgpack_object *);

/* Redirect protocol. */
int paxos_redirect(struct paxos_peer *, struct paxos_header *);
int proposer_ack_redirect(struct paxos_header *, msgpack_object *);
int acceptor_ack_redirect(struct paxos_header *, msgpack_object *);

/* Log sync protocol. */
int proposer_sync(void);
int acceptor_ack_sync(struct paxos_header *);
int proposer_ack_sync(struct paxos_header *, msgpack_object *);
int proposer_truncate(struct paxos_header *);
int acceptor_ack_truncate(struct paxos_header *, msgpack_object *);

#endif /* __PAXOS_PROTOCOL_H__ */
