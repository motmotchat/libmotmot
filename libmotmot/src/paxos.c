/**
 * paxos.c - Implementation of the Paxos consensus protocol
 */
#include "paxos.h"
#include "paxos_msgpack.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <glib.h>

static inline void
swap(void **p1, void **p2)
{
  void *tmp = *p1;
  *p1 = *p2;
  *p2 = tmp;
}

// Local protocol state
struct paxos_state pax;

// General Paxos functions
int paxos_redirect(struct paxos_peer *, struct paxos_header *);
int paxos_learn(struct paxos_instance *);

// Proposer operations
int proposer_prepare(void);
int proposer_ack_promise(struct paxos_header *, msgpack_object *);
int proposer_ack_request(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int proposer_decree(struct paxos_instance *);
int proposer_ack_accept(struct paxos_peer *, struct paxos_header *);
int proposer_commit(struct paxos_instance *);

// Acceptor operations
int acceptor_ack_prepare(struct paxos_peer *, struct paxos_header *);
int acceptor_promise(struct paxos_header *);
int acceptor_ack_decree(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_accept(struct paxos_header *);
int acceptor_ack_commit(struct paxos_header *);
int acceptor_ack_request(struct paxos_peer *,struct paxos_header *,
    msgpack_object *);


////////////////////////////////////////////////////////////////////////////////
//
//  Paxos protocol interface
//

/**
 * paxos_init - Initialize local Paxos state.
 */
void
paxos_init()
{
  pax.self_id = 0;  // XXX: Obviously wrong.
  pax.proposer = NULL;
  pax.ballot.id = 0;
  pax.ballot.gen = 0;
  pax.req_id = 0;

  pax.prep = NULL;

  LIST_INIT(&pax.alist);
  LIST_INIT(&pax.ilist);
  LIST_INIT(&pax.rlist);
}

/**
 * paxos_request - Broadcast an out-of-protocol message to all acceptors,
 * asking that they cache the message and requesting that the proposer
 * propose it as a decree.
 *
 * We send the request as a header along with a two-object array consisting
 * of a paxos_value (itself an array) and a msgpack raw (i.e., a string).
 */
int
paxos_request(dkind_t dkind, const char *msg, size_t len)
{
  struct paxos_header hdr;
  struct paxos_request *req;
  struct paxos_instance *inst;
  struct paxos_yak py;

  // Initialize a header.  We overload ph_inum to the ID of the acceptor who
  // we believe to be the proposer.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_REQUEST;
  hdr.ph_inum = pax.proposer->pa_paxid;

  // Allocate a request and initialize it.
  req = g_malloc0(sizeof(*req));
  req->pr_val.pv_dkind = dkind;
  req->pr_val.pv_reqid.id = pax.self_id;
  req->pr_val.pv_reqid.gen = (++pax.req_id);  // Increment our req_id.
  req->pr_size = len;

  if (msg != NULL) {
    memcpy(req->pr_data, msg, len);
  }

  // Add it to the request queue.
  request_insert(&pax.rlist, req);

  // Pack the request payload.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_request_pack(&py, req);

  // Broadcast the request
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // If we aren't the proposer, return.
  if (!is_proposer()) {
    return 0;
  }

  // We're the proposer, so allocate an instance and copy in the value from
  // the request.
  inst = g_malloc0(sizeof(*inst));
  memcpy(&inst->pi_val, &req->pr_val, sizeof(struct paxos_request));

  // Send a decree.
  return proposer_decree(inst);
}

static int
proposer_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // If this happens, it means that two proposers share a live connection
      // yet both think that they are at the head of the list of acceptors.
      // This list is the result of iterative Paxos commits, and hence a
      // proposer receiving an OP_PREPARE qualifies as an inconsistent state
      // of the system.
      g_critical("Proposer received OP_PREPARE.");
      break;

    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;

    case OP_DECREE:
      // XXX: If the decree is for a higher ballot number, we should probably
      // cry.
      g_error("Bad opcode OP_DECREE recieved by proposer. Redirecting...");
      paxos_redirect(source, hdr);
      break;

    case OP_ACCEPT:
      proposer_ack_accept(source, hdr);
      break;

    case OP_COMMIT:
      // TODO: Commit and relinquish presidency if the ballot is higher,
      // otherwise check if we have a decree of the given instance.  If
      // we do and /are not preparing/, redirect; otherwise (if we don't
      // or we do but we are preparing), commit it.
      break;

    case OP_REQUEST:
      proposer_ack_request(source, hdr, o);
      break;

    case OP_REDIRECT:
      // TODO: Decide what to do.
      break;
  }

  return TRUE;
}

static int
acceptor_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      acceptor_ack_prepare(source, hdr);
      break;

    case OP_PROMISE:
      g_error("Bad opcode OP_PROMISE recieved by acceptor. Redirecting...");
      paxos_redirect(source, hdr);
      break;

    case OP_DECREE:
      acceptor_ack_decree(source, hdr, o);
      break;

    case OP_ACCEPT:
      g_error("Bad opcode OP_ACCEPT recieved by acceptor. Redirecting...");
      paxos_redirect(source, hdr);
      break;

    case OP_COMMIT:
      acceptor_ack_commit(hdr);
      break;

    case OP_REQUEST:
      acceptor_ack_request(source, hdr, o);
      break;

    case OP_REDIRECT:
      // TODO: Think Real Hard (tm)
      break;
  }

  return TRUE;
}

/**
 * Handle a Paxos message.
 */
int
paxos_dispatch(struct paxos_peer *source, const msgpack_object *o)
{
  int retval;
  struct paxos_header *hdr;

  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size > 0 && o->via.array.size <= 2);

  // Unpack the Paxos header.  This may be clobbered by the proposer/acceptor
  // routines which the dispatch functions call.
  hdr = g_malloc0(sizeof(*hdr));
  paxos_header_unpack(hdr, o->via.array.ptr);

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, o->via.array.ptr + 1);
  } else {
    retval = acceptor_dispatch(source, hdr, o->via.array.ptr + 1);
  }

  g_free(hdr);
  return retval;
}


////////////////////////////////////////////////////////////////////////////////
//
//  General Paxos protocol functions
//

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
void
paxos_drop_connection(struct paxos_peer *source)
{
  struct paxos_acceptor *it;

  // Connection dropped; mark the acceptor as dead.
  LIST_FOREACH(it, &pax.alist, pa_le) {
    if (it->pa_peer == source) {
      it->pa_peer = NULL;
      break;
    }
  }
  paxos_peer_destroy(source);

  // Oh noes!  Did we lose the proposer?
  if (it->pa_paxid == pax.proposer->pa_paxid) {
    // Let's mark the new one.
    LIST_FOREACH(it, &pax.alist, pa_le) {
      if (it->pa_peer != NULL) {
        pax.proposer = it;
        break;
      }
    }

    // If we're the new proposer, send a prepare.
    if (is_proposer()) {
      proposer_prepare();
    }
  }
}

/**
 * paxos_redirect - Tell the sender of the message that we're not their
 * guy.  Tell them who their guy is.
 */
int
paxos_redirect(struct paxos_peer *source, struct paxos_header *recv_hdr)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_REDIRECT;
  hdr.ph_inum = 0;  // Not used.

  // Pack a payload, which includes the weird header we were sent.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_header_pack(&py, recv_hdr);

  // Send the payload.
  paxos_peer_send(source, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

int
paxos_learn(struct paxos_instance *inst)
{
  struct paxos_request *req;
  struct paxos_acceptor *acc;

  // Pull the request from the request queue if applicable.
  if (is_request(inst->pi_val.pv_dkind)) {
    req = request_find(&pax.rlist, inst->pi_val.pv_reqid);
  }

  // XXX: Act on the decree (e.g., display chat, record configs).
  switch (inst->pi_val.pv_dkind) {
    case DEC_NULL:
      break;

    case DEC_CHAT:
      break;

    case DEC_RENEW:
      break;

    case DEC_JOIN:
      // Initialize a new acceptor struct.  Its paxid will be the instance
      // number of its join decree, as will its rank.
      acc = g_malloc0(sizeof(*acc));
      acc->pa_paxid = inst->pi_hdr.ph_inum;
      acc->pa_rank = inst->pi_hdr.ph_inum;
      acc->pa_peer = NULL;

      // TODO: Initialize a paxos_peer via a callback.

      // TODO: If we are the proposer, send the new acceptor its paxid.

      // Append to our list.
      acceptor_insert(&pax.alist, acc);
      break;

    case DEC_PART:
      // TODO: Find the acceptor being removed (how?)

      // TODO: Destroy the paxos_peer via a callback.

      // Cleanup.
      LIST_REMOVE(&pax.alist, acc, pa_le);
      g_free(acc);
      break;
  }

  return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Proposer protocol
//

/*
 * Helper routine to obtain the instance on ilist with the closest instance
 * number >= inum.  We are passed in an iterator to simulate a continuation.
 */
static struct paxos_instance *
get_instance_lub(struct paxos_instance *it, struct instance_list *ilist,
    paxid_t inum)
{
  for (; it != (void *)ilist; it = LIST_NEXT(it, pi_le)) {
    if (inum <= it->pi_hdr.ph_inum) {
      break;
    }
  }

  return it;
}

/**
 * proposer_prepare - Broadcast a prepare message to all acceptors.
 *
 * The initiation of a prepare sequence is only allowed if we believe
 * ourselves to be the proposer.  Moreover, each proposer needs to make it
 * exactly one time.  Therefore, we call proposer_prepare() when and only
 * when:
 *  - We just lost the connection to the previous proposer.
 *  - We were next in line to be proposer.
 */
int
proposer_prepare()
{
  struct paxos_instance *it;
  struct paxos_header hdr;
  struct paxos_yak py;
  paxid_t inum;

  // If we were already preparing, get rid of that prepare.
  // XXX: I don't think this is possible.
  if (pax.prep != NULL) {
    g_free(pax.prep);
  }

  // Start a new ballot.
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Start a new prepare.
  pax.prep = g_malloc0(sizeof(*pax.prep));
  pax.prep->pp_nacks = 1;
  pax.prep->pp_inum = next_instance();  // Default to next instance.
  pax.prep->pp_first = NULL;

  // We let inum lag one list entry behind the iterator in our loop to
  // detect holes.
  inum = LIST_FIRST(&pax.ilist)->pi_hdr.ph_inum - 1;

  // Identify our first uncommitted or unrecorded instance (defaulting to
  // next_instance()).
  LIST_FOREACH(it, &pax.ilist, pi_le) {
    if (it->pi_hdr.ph_inum != inum + 1) {
      pax.prep->pp_inum = inum + 1;
      pax.prep->pp_first = LIST_PREV(it, pi_le);
      break;
    }
    if (it->pi_votes != 0) {
      pax.prep->pp_inum = it->pi_hdr.ph_inum;
      pax.prep->pp_first = it;
      break;
    }
    inum = it->pi_hdr.ph_inum;
  }

  if (pax.prep->pp_first == NULL) {
    pax.prep->pp_first = LIST_LAST(&pax.ilist);
  }

  // Initialize a Paxos header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_PREPARE;
  hdr.ph_inum = pax.prep->pp_inum;

  // Pack and broadcast the prepare.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_promise - Acknowledge an acceptor's promise.
 *
 * We acknowledge the promise by incrementing the number of acceptors who
 * have responded to the prepare, and by accounting for the acceptor's
 * votes on those decrees for which we do not have commit information.
 *
 * If we attain a majority of promises, we make decrees for all those
 * instances in which any acceptor voted, as well as null decrees for
 * any holes.  We then end the prepare.
 */
int
proposer_ack_promise(struct paxos_header *hdr, msgpack_object *o)
{
  msgpack_object *p, *pend, *r;
  struct paxos_instance *inst, *it;
  paxid_t inum;
  struct paxos_yak py;

  // If the promise is for some other ballot, just ignore it.  Acceptors
  // should only be sending a promise to us in response to a prepare from
  // us.  If we sent a redirect, by the time the acceptor got it, our
  // newer prepare would have arrived.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    return 0;
  }

  // Initialize loop variables.
  p = o->via.array.ptr;
  pend = o->via.array.ptr + o->via.array.size;

  it = pax.prep->pp_first;

  // Allocate a scratch instance.
  inst = g_malloc0(sizeof(*inst));

  // Loop through all the vote information.  Note that we assume the votes
  // are sorted by instance number.
  for (; p != pend; ++p) {
    r = p->via.array.ptr;

    // Unpack a instance.
    paxos_header_unpack(&inst->pi_hdr, r);
    paxos_value_unpack(&inst->pi_val, r + 1);
    inst->pi_votes = 1;

    // Get the closest instance with instance number >= the instance number
    // of inst.
    it = get_instance_lub(it, &pax.ilist, inst->pi_hdr.ph_inum);

    if (it == (void *)&pax.ilist) {
      // We didn't find an instance, so insert at the tail.
      LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);
    } else if (it->pi_hdr.ph_inum > inst->pi_hdr.ph_inum) {
      // We found an instance with a higher number, so insert before it.
      LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
    } else {
      // We found an instance of the same number.  If the existing instance
      // is NOT a commit, and if the new instance has a higher ballot number,
      // switch the new one in.
      if (it->pi_votes != 0 &&
          ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 0) {
        LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
        LIST_REMOVE(&pax.ilist, it, pi_le);
        swap((void **)&inst, (void **)&it);
      }
    }
  }

  // Free the scratch instance.
  g_free(inst);

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return 0;
  }

  it = pax.prep->pp_first;

  // For each Paxos instance for which we don't have a commit, send a decree.
  for (inum = pax.prep->pp_inum; ; ++inum) {
    // Get the closest instance with number >= inum.
    it = get_instance_lub(it, &pax.ilist, inum);

    // If we're at the end of the list, break.
    if (it == (void *)&pax.ilist) {
      break;
    }

    inst = NULL;

    if (it->pi_hdr.ph_inum > inum) {
      // Nobody in the quorum (including ourselves) has heard of this instance,
      // so make a null decree.
      inst = g_malloc0(sizeof(*inst));

      inst->pi_hdr.ph_ballot = pax.ballot;
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_hdr.ph_inum = inum;

      inst->pi_votes = 1;

      inst->pi_val.pv_dkind = DEC_NULL;
      inst->pi_val.pv_reqid.id = pax.self_id;
      inst->pi_val.pv_reqid.gen = pax.req_id;

      LIST_INSERT_BEFORE(&pax.ilist, it, inst, pi_le);
    } else if (it->pi_votes != 0) {
      // The quorum has seen this instance before, but it has not been
      // committed.  By the first part of ack_promise, the vote we have
      // here is the highest-ballot vote, so decree it again.
      inst = it;
      inst->pi_hdr.ph_ballot = pax.ballot;
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_votes = 1;
    }

    // Pack and broadcast the decree.
    if (inst != NULL) {
      paxos_payload_init(&py, 2);
      paxos_header_pack(&py, &(inst->pi_hdr));
      paxos_value_pack(&py, &(inst->pi_val));
      paxos_broadcast(UNYAK(&py));
      paxos_payload_destroy(&py);
    }
  }

  // Free the prepare and return.
  g_free(pax.prep);
  return 0;
}

/**
 * proposer_ack_request - Dispatch a request as a decree.
 *
 * Regardless of whether the requester thinks we are the proposer, we
 * benevolently handle their request.  However, we send a redirect if they
 * mistook another acceptor as having proposership.
 */
int
proposer_ack_request(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  struct paxos_request *req;
  struct paxos_instance *inst;

  // The requester overloads ph_inst to the acceptor it believes to be the
  // proposer.  If we are not identified as the proposer, send a redirect.
  if (hdr->ph_inum != pax.self_id) {
    paxos_redirect(source, hdr);
  }

  // Allocate a request and unpack into it.
  req = g_malloc0(sizeof(*req));
  paxos_request_unpack(req, o);

  // Add it to the request queue.
  request_insert(&pax.rlist, req);

  // Allocate an instance and copy in the value from the request.
  inst = g_malloc0(sizeof(*inst));
  memcpy(&inst->pi_val, &req->pr_val, sizeof(struct paxos_request));

  // Send a decree.
  return proposer_decree(inst);
}

/**
 * proposer_decree - Broadcast a decree.
 *
 * This function should be called with a paxos_instance struct that has a
 * well-defined value; however, the remaining fields will be rewritten.
 * If the instance was on a prepare list, it should be removed before
 * getting passed here.
 */
int
proposer_decree(struct paxos_instance *inst)
{
  struct paxos_yak py;

  // Update the header.
  inst->pi_hdr.ph_ballot = pax.ballot;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = next_instance();

  // Mark one vote.
  inst->pi_votes = 1;

  // Append to the ilist.
  LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);

  // Pack and broadcast the decree.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_value_pack(&py, &(inst->pi_val));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_accept - Acknowledge an acceptor's accept.
 *
 * Just increment the vote count of the correct Paxos instance and commit
 * if we have a majority.
 */
int
proposer_ack_accept(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // If the accept is for some other ballot, send a redirect.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    return paxos_redirect(source, hdr);
  }

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  inst->pi_votes++;

  // If we have a majority, send a commit message.
  if (inst->pi_votes >= MAJORITY) {
    return proposer_commit(inst);
  }

  return 0;
}

/**
 * proposer_commit - Broadcast a commit message for a given Paxos instance.
 *
 * This should only be called when we receive a majority vote for a decree.
 * We broadcast a commit message and mark the instance committed.
 */
int
proposer_commit(struct paxos_instance *inst)
{
  struct paxos_yak py;

  // Fix up the instance header.
  inst->pi_hdr.ph_opcode = OP_COMMIT;

  // Pack and broadcast the commit.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &(inst->pi_hdr));
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // Mark the instance committed.
  inst->pi_votes = 0;

  // Learn the value, i.e., act on the commit.
  paxos_learn(inst);

  return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Acceptor protocol
//

/**
 * acceptor_ack_prepare - Prepare for a new proposer.
 *
 * First, we check to see if we think that there's someone else who's more
 * eligible to be president.  If there exists such a person, redirect this
 * candidate to that person.
 *
 * If we think that this person would be a good proposer, prepare for their
 * presidency by sending them a list of our pending accepts from all
 * previous ballots.
 */
int
acceptor_ack_prepare(struct paxos_peer *source, struct paxos_header *hdr)
{
  // Only promise if we think the preparer is the proposer and if the new
  // ballot number is greater than the existing one.
  if (ballot_compare(hdr->ph_ballot, pax.ballot) > 0 ||
      pax.proposer->pa_peer != source) {
    return paxos_redirect(source, hdr);
  }

  return acceptor_promise(hdr);
}

/**
 * acceptor_promise - Promise fealty to our new overlord.
 *
 * Send the proposer a promise to accept their decrees in perpetuity.  We
 * also send them a list of all of the accepts we made in previous ballots.
 * The data format looks like:
 *
 *    struct {
 *      paxos_header header;
 *      struct {
 *        paxos_header promise_header;
 *        paxos_value promise_value;
 *      } promises[n];
 *    }
 *
 * where we pack the internal structs as two-element arrays.
 */
int
acceptor_promise(struct paxos_header *hdr)
{
  size_t count;
  struct paxos_instance *it;
  struct paxos_yak py;

  // Set our ballot to the one given in the prepare.
  pax.ballot.id = hdr->ph_ballot.id;
  pax.ballot.gen = hdr->ph_ballot.gen;

  // Start off the payload with the header.
  hdr->ph_opcode = OP_PROMISE;
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);

  count = 0;

  // Determine how many accepts we need to send back.
  LIST_FOREACH_REV(it, &pax.ilist, pi_le) {
    count++;
    if (hdr->ph_inum >= it->pi_hdr.ph_inum) {
      break;
    }
  }

  // Start the payload of promises.
  paxos_payload_begin_array(&py, count);

  // For each instance starting at the iterator, pack an array containing
  // information about our accept.
  for (; it != (void *)&pax.ilist; it = LIST_NEXT(it, pi_le)) {
    paxos_payload_begin_array(&py, 2);
    paxos_header_pack(&py, &it->pi_hdr);
    paxos_value_pack(&py, &it->pi_val);
  }

  // Send off our payload.
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_decree - Accept a value for a Paxos instance.
 *
 * Move to commit the given value for the given Paxos instance.  After this
 * step, we consider the value accepted and will only accept this particular
 * value going forward.  We do not consider the decree "learned," however,
 * so we don't release it to the outside world just yet.
 */
int
acceptor_ack_decree(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  int cmp;
  struct paxos_instance *inst;

  // Check the ballot on the message.
  cmp = ballot_compare(hdr->ph_ballot, pax.ballot);
  if (cmp < 0) {
    // If the decree has a lower ballot number, send a redirect.
    return paxos_redirect(source, hdr);
  } else if (cmp > 0) {
    // XXX: What if the decree has a higher ballot?
  }

  // See if we have seen this instance for another ballot.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  if (inst == NULL) {
    // We haven't seen this instance, so initialize a new one.
    inst = g_malloc0(sizeof(*inst));
    memcpy(&inst->pi_hdr, hdr, sizeof(hdr));
    paxos_value_unpack(&inst->pi_val, o);
    inst->pi_votes = 1; // For acceptors, != 0 just means not committed.

    // Insert the new instance into the ilist.
    inst = instance_insert(&pax.ilist, inst);
  } else {
    // We found an instance of the same number.  If the existing instance
    // is NOT a commit, and if the new instance has a higher ballot number,
    // switch the new one in.
    if (inst->pi_votes != 0 &&
        ballot_compare(hdr->ph_ballot, inst->pi_hdr.ph_ballot) > 0) {
      memcpy(&inst->pi_hdr, hdr, sizeof(hdr));
      paxos_value_unpack(&inst->pi_val, o);
    }
  }

  // We've created a new instance struct as necessary, so accept.
  return acceptor_accept(hdr);
}

/**
 * acceptor_accept - Notify the proposer we accept their decree.
 */
int
acceptor_accept(struct paxos_header *hdr)
{
  struct paxos_yak py;

  // Pack a header.
  hdr->ph_opcode = OP_ACCEPT;
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, hdr);

  // Send the payload.
  paxos_send_to_proposer(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}


/**
 * acceptor_ack_commit - Commit ("learn") a value.
 *
 * Commit this value as a permanent learned value, and notify listeners of the
 * value payload.
 */
int
acceptor_ack_commit(struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // Retrieve the instance struct corresponding to the inum.
  inst = instance_find(&pax.ilist, hdr->ph_inum);

  // XXX: I don't think we need to check that the ballot numbers match
  // because Paxos is supposed to guarantee that a commit command from the
  // proposer will always be consistent.  For the same reason, we shouldn't
  // have to check that inst might be NULL.

  // Mark the value as committed.
  inst->pi_votes = 0;

  // Learn the value, i.e., act on the commit.
  paxos_learn(inst);

  return 0;
}

/*
 * acceptor_ack_request - Cache a requester's message, waiting for the
 * proposer to decree it.
 */
int
acceptor_ack_request(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  struct paxos_request *req;

  // The requester overloads ph_inst to the acceptor it believes to be the
  // proposer.  If we are incorrectly identified as the proposer, send a
  // redirect.
  if (hdr->ph_inum == pax.self_id && is_proposer()) {
    paxos_redirect(source, hdr);
  }

  // Allocate a request and unpack into it.
  req = g_malloc0(sizeof(*req));
  paxos_request_unpack(req, o);

  // Add it to the request queue.
  request_insert(&pax.rlist, req);

  return 0;
}
