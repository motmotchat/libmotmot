/**
 * paxos_helper.h - Paxos protocol helper routines.
 */
#ifndef __PAXOS_HELPER_H__
#define __PAXOS_HELPER_H__

#include "paxos.h"

#include <glib.h>
#include <msgpack.h>

/* Convenience functions. */
inline int is_proposer();
inline paxid_t next_instance();
int request_needs_cached(dkind_t dkind);

/* Paxid pair comparison functions. */
int ppair_compare(ppair_t, ppair_t);
int ballot_compare(ballot_t, ballot_t);
int compare_reqid(reqid_t, reqid_t);

/* List helpers. */
struct paxos_acceptor *acceptor_find(struct acceptor_list *, paxid_t);
struct paxos_acceptor *acceptor_insert(struct acceptor_list *,
    struct paxos_acceptor *);
struct paxos_instance *instance_find(struct instance_list *, paxid_t);
struct paxos_instance *instance_insert(struct instance_list *,
    struct paxos_instance *);
struct paxos_request *request_find(struct request_list *, reqid_t);
struct paxos_request *request_insert(struct request_list *,
    struct paxos_request *);

struct paxos_instance *get_instance_lub(struct paxos_instance *it,
    struct instance_list *ilist, paxid_t inum);
paxid_t ilist_first_hole(struct paxos_instance **inst,
    struct instance_list *ilist, paxid_t start);

/* Paxos message sending. */
int paxos_broadcast(const char *, size_t);
int paxos_send(struct paxos_acceptor *, const char *, size_t);
int paxos_send_to_proposer(const char *, size_t);

#endif /* __PAXOS_HELPER_H__ */
