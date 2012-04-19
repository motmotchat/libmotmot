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
int proposer_welcome(struct paxos_acceptor *);
int proposer_sync(void);
int proposer_ack_sync(struct paxos_header *, msgpack_object *);
int proposer_truncate(struct paxos_header *);

// Acceptor operations
int acceptor_ack_welcome(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_ack_prepare(struct paxos_peer *, struct paxos_header *);
int acceptor_promise(struct paxos_header *);
int acceptor_ack_decree(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_accept(struct paxos_header *);
int acceptor_ack_commit(struct paxos_header *);
int acceptor_ack_request(struct paxos_peer *, struct paxos_header *,
    msgpack_object *);
int acceptor_hello(struct paxos_acceptor *);
int acceptor_ack_hello(struct paxos_peer *, struct paxos_header *);
int acceptor_ack_sync(struct paxos_header *);
int acceptor_ack_truncate(struct paxos_header *, msgpack_object *);


////////////////////////////////////////////////////////////////////////////////
//
//  Paxos protocol interface
//

/**
 * paxos_init - Initialize local Paxos state.
 *
 * Most of our state is worthless until we are welcomed to the system.
 */
void
paxos_init(connect_t connect, struct learn_table *learn)
{
  pax.self_id = 0;
  pax.req_id = 0;
  pax.proposer = NULL;
  pax.ballot.id = 0;
  pax.ballot.gen = 0;
  pax.istart = 0;

  pax.prep = NULL;
  pax.sync = NULL;
  pax.sync_id = 0;

  LIST_INIT(&pax.alist);
  LIST_INIT(&pax.ilist);
  LIST_INIT(&pax.rlist);

  pax.connect = connect;
  pax.learn.chat = learn->chat;
  pax.learn.join = learn->join;
  pax.learn.part = learn->part;
}

/**
 * paxos_start - Start up the Paxos protocol with ourselves as the proposer.
 *
 * Currently we only support one universal execution of the protocol, so
 * everybody had better be in agreement about the fact that we get to start
 * it up.
 */
void
paxos_start()
{
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  // Give ourselves ID 1.
  pax.self_id = 1;

  // Prepare a new ballot.  Hey, we accept the prepare!  Hoorah.
  pax.ballot.id = 1;
  pax.ballot.gen = 1;

  // Initialize an initial commit.
  inst = g_malloc(sizeof(*inst));

  inst->pi_hdr.ph_ballot.id = pax.ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax.ballot.gen;
  inst->pi_hdr.ph_opcode = OP_DECREE;
  inst->pi_hdr.ph_inum = 1;

  inst->pi_votes = 0;

  inst->pi_val.pv_dkind = DEC_JOIN;
  inst->pi_val.pv_reqid.id = pax.self_id;
  inst->pi_val.pv_reqid.gen = pax.req_id;

  // Add it to our ilist to mark our JOIN.
  LIST_INSERT_HEAD(&pax.ilist, inst, pi_le);
  pax.istart = 1;

  // Add ourselves to the acceptor list.
  acc = g_malloc(sizeof(*acc));
  acc->pa_paxid = pax.self_id;
  acc->pa_peer = NULL;
  LIST_INSERT_HEAD(&pax.alist, acc, pa_le);

  // Set ourselves as the proposer.
  pax.proposer = acc;
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
  int needs_cached;
  struct paxos_header hdr;
  struct paxos_request *req;
  struct paxos_instance *inst;
  struct paxos_yak py;

  if (pax.self_id == 0) {
    return -1;
  }

  // Do we need to keep this request around?
  needs_cached = request_needs_cached(dkind);

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
  req->pr_val.pv_extra = 0; // Always 0 for requests.
  req->pr_size = len;

  if (msg != NULL) {
    req->pr_data = g_memdup(msg, len);
  }

  // Add it to the request queue if needed.
  if (needs_cached) {
    request_insert(&pax.rlist, req);
  }

  if (!is_proposer() || needs_cached) {
    // We need to send iff either we are not the proposer or the request
    // has nontrivial data.
    paxos_payload_init(&py, 2);
    paxos_header_pack(&py, &hdr);
    paxos_request_pack(&py, req);

    if (!needs_cached) {
      paxos_send_to_proposer(UNYAK(&py));
    } else {
      paxos_broadcast(UNYAK(&py));
    }

    paxos_payload_destroy(&py);
  }

  // We're done if we're not the proposer.
  if (!is_proposer()) {
    return 0;
  }

  // We're the proposer, so allocate an instance and copy in the value from
  // the request.
  inst = g_malloc0(sizeof(*inst));
  memcpy(&inst->pi_val, &req->pr_val, sizeof(req->pr_val));

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
      g_error("Proposer received OP_PREPARE.\n");
      break;

    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;

    case OP_DECREE:
      // XXX: If the decree is for a higher ballot number, we should probably
      // cry.
      g_error("Bad opcode OP_DECREE recieved by proposer. Redirecting...\n");
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

    case OP_WELCOME:
      // Ignore.
      break;

    case OP_HELLO:
      // This shouldn't be possible; all the acceptors who would say hello to
      // us are higher-ranked than us when we join the protocol, and if we are
      // the proposer, they have all dropped.
      g_error("Proposer received OP_HELLO.\n");
      break;

    case OP_SYNC:
      proposer_ack_sync(hdr, o);
      break;

    case OP_TRUNCATE:
      // Holy fucking shit what is happening.
      g_error("Proposer received OP_TRUNCATE.\n");
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
      g_error("Bad opcode OP_PROMISE recieved by acceptor. Redirecting...\n");
      paxos_redirect(source, hdr);
      break;

    case OP_DECREE:
      acceptor_ack_decree(source, hdr, o);
      break;

    case OP_ACCEPT:
      g_error("Bad opcode OP_ACCEPT recieved by acceptor. Redirecting...\n");
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

    case OP_WELCOME:
      acceptor_ack_welcome(source, hdr, o);
      break;

    case OP_HELLO:
      acceptor_ack_hello(source, hdr);
      break;

    case OP_SYNC:
      acceptor_ack_sync(hdr);
      break;

    case OP_TRUNCATE:
      acceptor_ack_truncate(hdr, o);
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
      if (it->pa_paxid == pax.self_id || it->pa_peer != NULL) {
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

/**
 * paxos_learn - Do something useful with the value of a commit.
 */
int
paxos_learn(struct paxos_instance *inst)
{
  struct paxos_request *req;
  struct paxos_acceptor *acc;

  // Pull the request from the request queue if applicable.
  if (request_needs_cached(inst->pi_val.pv_dkind)) {
    req = request_find(&pax.rlist, inst->pi_val.pv_reqid);
  }

  // Act on the decree (e.g., display chat, record acceptor list changes).
  switch (inst->pi_val.pv_dkind) {
    case DEC_NULL:
      break;

    case DEC_CHAT:
      // Invoke client learning callback.
      pax.learn.chat(req->pr_data, req->pr_size);
      break;

    case DEC_JOIN:
      // Initialize a new acceptor struct.  Its paxid is the instance number
      // of the JOIN.
      acc = g_malloc0(sizeof(*acc));
      acc->pa_paxid = inst->pi_hdr.ph_inum;

      // Initialize a paxos_peer via a callback.
      acc->pa_peer = paxos_peer_init(pax.connect(req->pr_data, req->pr_size));

      // Append to our list.
      acceptor_insert(&pax.alist, acc);

      // If we are the proposer, send the new acceptor its paxid.
      if (is_proposer()) {
        proposer_welcome(acc);
      } else {
        acceptor_hello(acc);
      }

      // Invoke client learning callback.
      pax.learn.join(req->pr_data, req->pr_size);
      break;

    case DEC_PART:
      // The pv_extra field tells us who is PARTing (possibly a forced PART).
      // If it is 0, it is a user request to self-PART.
      if (inst->pi_val.pv_extra == 0) {
        inst->pi_val.pv_extra = inst->pi_val.pv_reqid.id;
      }

      // Pull the acceptor from the alist.
      acc = acceptor_find(&pax.alist, inst->pi_val.pv_extra);

      // Cleanup.
      paxos_peer_destroy(acc->pa_peer);
      LIST_REMOVE(&pax.alist, acc, pa_le);
      g_free(acc);

      // Invoke client learning callback.
      pax.learn.part(req->pr_data, req->pr_size);
      break;

    default:
      break;
  }

  return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Static ilist helper routines
//

/**
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
 * Starting at a given instance number, crawl along an ilist until we find
 * a hole, i.e., an instance which has either not been recorded or not been
 * committed.  We return its instance number, along with closest-numbered
 * instance structure that has number <= the hole.
 */
static paxid_t
ilist_first_hole(struct paxos_instance **inst, struct instance_list *ilist,
    paxid_t start)
{
  paxid_t inum;
  struct paxos_instance *it;

  // Obtain the first instance with inum >= start.
  it = get_instance_lub(LIST_FIRST(ilist), ilist, start);

  // If its inum != start, then start itself represents a hole.
  if (it->pi_hdr.ph_inum != start) {
    *inst = LIST_PREV(it, pi_le);
    if (*inst == (void *)ilist) {
      *inst = NULL;
    }
    return start;
  }

  // If the start instance is uncommitted, it's the hole we want.
  if (it->pi_votes != 0) {
    *inst = it;
    return start;
  }

  // We let inum lag one list entry behind the iterator in our loop to
  // detect holes; we use *inst to detect success.
  inum = start - 1;
  *inst = NULL;

  // Identify our first uncommitted or unrecorded instance.
  LIST_FOREACH(it, ilist, pi_le) {
    if (it->pi_hdr.ph_inum != inum + 1) {
      // We know there exists a previous element because start corresponded
      // to some existing instance.
      *inst = LIST_PREV(it, pi_le);
      return inum + 1;
    }
    if (it->pi_votes != 0) {
      // We found an uncommitted instance, so return it.
      *inst = it;
      return it->pi_hdr.ph_inum;
    }
    inum = it->pi_hdr.ph_inum;
  }

  // Default to the next unused instance.
  if (*inst == NULL) {
    *inst = LIST_LAST(ilist);
    return LIST_LAST(ilist)->pi_hdr.ph_inum + 1;
  }

  // Impossible.
  return 0;
}

/**
 * Truncate an ilist up to (but not including) a given inum.
 */
static void
ilist_truncate_prefix(struct instance_list *ilist, paxid_t inum)
{
  struct paxos_instance *it, *prev;

  prev = NULL;
  LIST_FOREACH(it, ilist, pi_le) {
    if (prev != NULL) {
      LIST_REMOVE(ilist, prev, pi_le);
      g_free(prev);
    }
    if (it->pi_hdr.ph_inum >= inum) {
      break;
    }
    prev = it;
  }
}


////////////////////////////////////////////////////////////////////////////////
//
//  Proposer protocol
//

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
  struct paxos_header hdr;
  struct paxos_yak py;

  // If we were already preparing, get rid of that prepare.
  // XXX: I don't think this is possible.
  if (pax.prep != NULL) {
    g_free(pax.prep);
    pax.prep = NULL;
  }

  // Start a new ballot.
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Start a new prepare.
  pax.prep = g_malloc0(sizeof(*pax.prep));
  pax.prep->pp_nacks = 1;
  pax.prep->pp_first = NULL;

  // Obtain the first hole.
  pax.prep->pp_hole = ilist_first_hole(&pax.prep->pp_first, &pax.ilist,
                                       pax.istart);

  // Initialize a Paxos header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_PREPARE;
  hdr.ph_inum = pax.prep->pp_hole;

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
  msgpack_object *p, *pend;
  struct paxos_instance *inst, *it;
  paxid_t inum;
  struct paxos_yak py;
  struct paxos_acceptor *acc;

  // If the promise is for some other ballot, just ignore it.  Acceptors
  // should only be sending a promise to us in response to a prepare from
  // us.  If we sent a redirect, by the time the acceptor got it, our
  // newer prepare would have arrived.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    return 0;
  }

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);

  // Initialize loop variables.
  p = o->via.array.ptr;
  pend = o->via.array.ptr + o->via.array.size;
  it = pax.prep->pp_first;

  // Allocate a scratch instance.
  inst = g_malloc0(sizeof(*inst));

  // Loop through all the vote information.  Note that we assume the votes
  // are sorted by instance number.
  for (; p != pend; ++p) {
    // Unpack a instance.
    paxos_instance_unpack(inst, p);
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
  for (inum = pax.prep->pp_hole; ; ++inum) {
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
      // committed.  By the first part of ack_promise, the vote we have here
      // is the highest-ballot vote, so decree it again.
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

  // Free the prepare.
  g_free(pax.prep);
  pax.prep = NULL;

  // Forceably PART any dropped acceptors we still have in our acceptor list.
  LIST_FOREACH(acc, &pax.alist, pa_le) {
    if (acc->pa_peer == NULL) {
      // Initialize a new instance.
      inst = g_malloc0(sizeof(*inst));
      inst->pi_val.pv_dkind = DEC_PART;
      inst->pi_val.pv_reqid.id = pax.self_id;
      inst->pi_val.pv_reqid.gen = pax.req_id;
      inst->pi_val.pv_extra = acc->pa_paxid;

      // Decree it.
      proposer_decree(inst);
    }
  }

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

  // Add it to the request queue if needed.
  if (request_needs_cached(req->pr_val.pv_dkind)) {
    request_insert(&pax.rlist, req);
  }

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
  inst->pi_hdr.ph_ballot.id = pax.ballot.id;
  inst->pi_hdr.ph_ballot.gen = pax.ballot.gen;
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

  // Do we constitute a majority ourselves?  If so, commit!
  if (inst->pi_votes >= MAJORITY) {
    return proposer_commit(inst);
  }

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
  if (ballot_compare(pax.ballot, hdr->ph_ballot)) {
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

/**
 * proposer_welcome - Welcome new protocol participant by passing along
 *
 * struct {
 *   paxos_header hdr;
 *   struct {
 *     paxid_t istart;
 *     paxos_acceptor alist[];
 *     paxos_instance ilist[];
 *   } init_info;
 * }
 *
 * The header contains the ballot information and the new acceptor's paxid
 * in ph_inum (since its inum is just the instance number of its JOIN).
 * We also send over our list of acceptors and instances to start the new
 * acceptor off.
 *
 * XXX: We should sync when we add a participant.
 */
int
proposer_welcome(struct paxos_acceptor *acc)
{
  struct paxos_header hdr;
  struct paxos_acceptor *acc_it;
  struct paxos_instance *inst_it;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_WELCOME;

  // The new acceptor's ID is also the instance number of its JOIN.
  hdr.ph_inum = acc->pa_paxid;

  // Pack the header into a new payload.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);

  // Start off the info payload with the istart.
  paxos_payload_begin_array(&py, 3);
  paxos_paxid_pack(&py, pax.istart);

  // Pack the entire alist.  Hopefully we don't have too many un-reaped
  // dropped acceptors (we shouldn't).
  paxos_payload_begin_array(&py, LIST_COUNT(&pax.alist));
  LIST_FOREACH(acc_it, &pax.alist, pa_le) {
    paxos_acceptor_pack(&py, acc_it);
  }

  // Pack the entire ilist.  We should have just synced, so this shouldn't
  // be unconscionably large.
  paxos_payload_begin_array(&py, LIST_COUNT(&pax.ilist));
  LIST_FOREACH(inst_it, &pax.ilist, pi_le) {
    paxos_instance_pack(&py, inst_it);
  }

  // Send the welcome.
  paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_sync - Send a sync command to all acceptors.
 *
 * For a sync to succeed, all acceptors need to tell us the location of the
 * first hole in their (hopefully mostly contiguous) list of committed
 * Paxos instances.  We take the minimum of these values and then command
 * everyone to truncate everything before the first hole.
 *
 * XXX: Implement retry protocol to obtain missing commits.
 */
int
proposer_sync()
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // If we're already syncing, increment the skip counter.
  // XXX: Do something with this, perhaps?
  if (pax.sync != NULL) {
    pax.sync->ps_skips++;
  }

  // Create a new sync.
  pax.sync = g_malloc0(sizeof(*(pax.sync)));
  pax.sync->ps_total = LIST_COUNT(&pax.alist);
  pax.sync->ps_nacks = 1; // Counting ourselves.
  pax.sync->ps_skips = 0;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_SYNC;
  hdr.ph_inum = (++pax.sync_id);  // Sync number.

  // Pack and broadcast the sync.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * proposer_ack_sync - Update sync state based on acceptor's reply.
 */
int
proposer_ack_sync(struct paxos_header *hdr, msgpack_object *o)
{
  paxid_t hole;

  // Ignore syncs for older sync commands.
  if (hdr->ph_inum != pax.sync_id) {
    return 0;
  }

  // Update our knowledge of the first commit hole.
  paxos_paxid_unpack(&hole, o);
  if (hole < pax.sync->ps_hole) {
    pax.sync->ps_hole = hole;
  }

  // Increment nacks and command a truncate if the sync is over.
  pax.sync->ps_nacks++;
  if (pax.sync->ps_nacks == pax.sync->ps_total) {
    return proposer_truncate(hdr);
  }

  return 0;
}

/**
 * proposer_truncate - Command all acceptors to drop the contiguous prefix
 * of Paxos instances for which they all have committed.
 */
int
proposer_truncate(struct paxos_header *hdr)
{
  paxid_t hole;
  struct paxos_instance *inst;
  struct paxos_yak py;

  // Obtain our own first instance hole.
  hole = ilist_first_hole(&inst, &pax.ilist, pax.istart);
  if (hole < pax.sync->ps_hole) {
    pax.sync->ps_hole = hole;
  }

  // Make this hole our new istart.
  assert(pax.sync->ps_hole <= pax.istart);
  pax.istart = pax.sync->ps_hole;

  // Do the truncate (< pax.istart).
  ilist_truncate_prefix(&pax.ilist, pax.istart);

  // Pack and broadcast a truncate command.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, pax.istart);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  // End the sync.
  g_free(pax.sync);
  pax.sync = NULL;

  return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
//  Acceptor protocol
//

/**
 * acceptor_ack_welcome - Be welcomed to the Paxos system.
 *
 * This allows us to populate our ballot, alist, and ilist, as well as to
 * learn our assigned paxid.
 */
int
acceptor_ack_welcome(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  msgpack_object *arr, *p, *pend;
  struct paxos_acceptor *acc;
  struct paxos_instance *inst;

  // Set our local state.
  pax.ballot.id = hdr->ph_ballot.id;
  pax.ballot.gen = hdr->ph_ballot.gen;
  pax.self_id = hdr->ph_inum;

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 3);
  arr = o->via.array.ptr;

  // Unpack the istart.
  assert(arr->type == MSGPACK_OBJECT_POSITIVE_INTEGER);
  pax.istart = arr->via.u64;

  arr++;

  // Make sure the alist is well-formed...
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  p = arr->via.array.ptr;
  pend = arr->via.array.ptr + arr->via.array.size;

  // ...and populate our alist.
  for (; p != pend; ++p) {
    acc = g_malloc0(sizeof(*acc));
    paxos_acceptor_unpack(acc, p);
    LIST_INSERT_TAIL(&pax.alist, acc, pa_le);

    // Set the proposer correctly.
    if (acc->pa_paxid == hdr->ph_ballot.id) {
      pax.proposer = acc;
      pax.proposer->pa_peer = source;
    }
  }

  arr++;

  // Make sure the ilist is well-formed...
  assert(arr->type == MSGPACK_OBJECT_ARRAY);
  p = arr->via.array.ptr;
  pend = arr->via.array.ptr + arr->via.array.size;

  // ...and populate our ilist.
  for (; p != pend; ++p) {
    inst = g_malloc0(sizeof(*inst));
    paxos_instance_unpack(inst, p);
    LIST_INSERT_TAIL(&pax.ilist, inst, pi_le);
  }

  return 0;
}

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

  // Pack all the instances starting at the lowest-numbered instance
  // requested.
  for (; it != (void *)&pax.ilist; it = LIST_NEXT(it, pi_le)) {
    paxos_instance_pack(&py, it);
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
    memcpy(&inst->pi_hdr, hdr, sizeof(*hdr));
    paxos_value_unpack(&inst->pi_val, o);
    inst->pi_votes = 1; // For acceptors, != 0 just means not committed.

    // Insert the new instance into the ilist.
    inst = instance_insert(&pax.ilist, inst);

    // Accept the decree.
    return acceptor_accept(hdr);
  } else {
    // We found an instance of the same number.  If the existing instance
    // is NOT a commit, and if the new instance has a higher ballot number,
    // switch the new one in.
    if (inst->pi_votes != 0 &&
        ballot_compare(hdr->ph_ballot, inst->pi_hdr.ph_ballot) > 0) {
      memcpy(&inst->pi_hdr, hdr, sizeof(hdr));
      paxos_value_unpack(&inst->pi_val, o);

      // Accept the decree.
      return acceptor_accept(hdr);
    }
  }

  // XXX: If we found a committed decree, probably inform the proposer?  If
  // we just found an (uncommitted) decree with a higher ballot, we should
  // check whether or not we accept by comparing values.
  return 0;
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

/**
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

/**
 * acceptor_hello - Let a new proposer know our identity.
 */
int
acceptor_hello(struct paxos_acceptor *acc)
{
  struct paxos_header hdr;
  struct paxos_yak py;

  // Initialize the header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_HELLO;
  hdr.ph_inum = pax.self_id;  // Overloaded with our acceptor ID.

  // Pack and send the hello.
  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &hdr);
  paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

/**
 * acceptor_ack_hello - Record the identity of a fellow acceptor.
 */
int
acceptor_ack_hello(struct paxos_peer *source, struct paxos_header *hdr)
{
  struct paxos_acceptor *acc;

  // Find the acceptor with the OP_HELLO's from ID.
  acc = acceptor_find(&pax.alist, hdr->ph_inum);

  if (acc == NULL) {
    // XXX: This might be possible if a JOIN was decreed just before we were
    // added, and the commit of that JOIN occurred before we were welcomed
    // by the proposer.
  }

  // Associate the peer to the acceptor.
  acc->pa_peer = source;

  return 0;
}

/**
 * acceptor_ack_sync - Respond to the sync request of a proposer.
 *
 * We respond by sending our the first hole in our instance list.
 */
int
acceptor_ack_sync(struct paxos_header *hdr)
{
  paxid_t hole;
  struct paxos_instance *inst;
  struct paxos_yak py;

  // Obtain the hole.
  hole = ilist_first_hole(&inst, &pax.ilist, pax.istart);

  // Pack and broadcast the response.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_paxid_pack(&py, hole);
  paxos_broadcast(UNYAK(&py));
  paxos_payload_destroy(&py);

  return 0;
}

int
acceptor_ack_truncate(struct paxos_header *hdr, msgpack_object *o)
{
  // Unpack the new istart.
  paxos_paxid_unpack(&pax.istart, o);

  // Do the truncate (< pax.istart).
  ilist_truncate_prefix(&pax.ilist, pax.istart);

  return 0;
}
