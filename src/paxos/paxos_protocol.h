/**
 * paxos_protocol.h - Prototypes for protocol internal functions.
 */
#ifndef __PAXOS_PROTOCOL_H__
#define __PAXOS_PROTOCOL_H__

#include <glib.h>
#include <msgpack.h>

#include "paxos.h"

/* Dispatch. */
int paxos_dispatch(struct paxos_peer *, const msgpack_object *);

/* Learner operations. */
int paxos_commit(struct paxos_instance *);
int paxos_learn(struct paxos_instance *, struct paxos_request *);

/* Proposer operations. */
int proposer_prepare(struct paxos_acceptor *);
int proposer_ack_promise(struct paxos_header *, msgpack_object *);
int proposer_decree(struct paxos_instance *);
int proposer_ack_accept(struct paxos_header *);
int proposer_commit(struct paxos_instance *);

/* Acceptor operations. */
int acceptor_ack_prepare(struct paxos_peer *, struct paxos_header *);
int acceptor_promise(struct paxos_header *);
int acceptor_ack_decree(struct paxos_header *, msgpack_object *);
int acceptor_accept(struct paxos_header *);
int acceptor_ack_commit(struct paxos_header *, msgpack_object *);

/* Participant initiation protocol. */
int proposer_welcome(struct paxos_acceptor *);
int acceptor_ack_welcome(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int paxos_hello(struct paxos_acceptor *);
int paxos_ack_hello(struct paxos_peer *, struct paxos_header *);

/* Out-of-band request protocol. */
int proposer_ack_request(struct paxos_header *, msgpack_object *);
int acceptor_ack_request(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int paxos_retrieve(struct paxos_instance *);
int paxos_ack_retrieve(struct paxos_header *, msgpack_object *);
int paxos_resend(struct paxos_acceptor *, struct paxos_header *,
    struct paxos_request *);
int paxos_ack_resend(struct paxos_header *, msgpack_object *);

/* Reconnect protocol. */
int acceptor_redirect(struct paxos_peer *, struct paxos_header *);
int proposer_ack_redirect(struct paxos_header *, msgpack_object *);
int acceptor_refuse(struct paxos_peer *, struct paxos_header *,
    struct paxos_request *);
int acceptor_ack_refuse(struct paxos_header *, msgpack_object *);
int acceptor_reject(struct paxos_header *);
int proposer_ack_reject(struct paxos_header *);

/* Retry protocol. */
int acceptor_retry(paxid_t);
int proposer_ack_retry(struct paxos_header *);
int proposer_recommit(struct paxos_header *, struct paxos_instance *);
int acceptor_ack_recommit(struct paxos_header *, msgpack_object *);

/* Log sync protocol. */
int proposer_sync(void);
int acceptor_ack_sync(struct paxos_header *);
int acceptor_last(struct paxos_header *);
int proposer_ack_last(struct paxos_header *, msgpack_object *);
int proposer_truncate(struct paxos_header *);
int acceptor_ack_truncate(struct paxos_header *, msgpack_object *);

/* Connection establishment continuations. */
int continue_welcome(GIOChannel *, void *);
int continue_ack_welcome(GIOChannel *, void *);
int continue_ack_redirect(GIOChannel *, void *);
int continue_ack_refuse(GIOChannel *, void *);
int continue_ack_reject(GIOChannel *, void *);

#endif /* __PAXOS_PROTOCOL_H__ */
