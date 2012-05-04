/**
 * paxos_request.c - Out-of-band requests for Paxos.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "list.h"

#include <assert.h>
#include <glib.h>

extern int proposer_decree_part(struct paxos_acceptor *);

/**
 * proposer_decree_request - Helper function for proposers to decree requests.
 */
static int
proposer_decree_request(struct paxos_request *req)
{
  struct paxos_instance *inst;

  // Allocate an instance and copy in the value from the request.
  inst = g_malloc0(sizeof(*inst));
  memcpy(&inst->pi_val, &req->pr_val, sizeof(req->pr_val));

  // Send a decree if we're not preparing; if we are, defer it.
  if (pax.prep != NULL) {
    LIST_INSERT_TAIL(&pax.idefer, inst, pi_le);
    return 0;
  } else {
    return proposer_decree(inst);
  }
}

/**
 * paxos_request - Request that the proposer make a decree for us.
 *
 * If the request has data attached to it, we broadcast an out-of-band message
 * to all acceptors, asking that they cache our message until the proposer
 * commits it.
 *
 * We send the request as a header along with a two-object array consisting
 * of a paxos_value (itself an array) and a msgpack raw (i.e., a data
 * string).
 */
int
paxos_request(struct paxos_state *session, dkind_t dkind, const void *msg,
    size_t len)
{
  int r, needs_cached;
  struct paxos_header hdr;
  struct paxos_request *req;
  struct paxos_yak py;

  // We can't make requests if we're not part of a protocol.
  if (pax.self_id == 0) {
    return 1;
  }

  // Do we need to cache this request?
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
  req->pr_data = g_memdup(msg, len);

  // Add it to the request cache if needed.
  if (needs_cached) {
    request_insert(&pax.rcache, req);
  }

  if (!is_proposer() || needs_cached) {
    // We need to send iff either we are not the proposer or the request
    // has nontrivial data.
    paxos_payload_init(&py, 2);
    paxos_header_pack(&py, &hdr);
    paxos_request_pack(&py, req);

    // Broadcast only if it needs caching.
    if (!needs_cached) {
      r = paxos_send_to_proposer(UNYAK(&py));
    } else {
      r = paxos_broadcast(UNYAK(&py));
    }

    paxos_payload_destroy(&py);
    if (r) {
      return r;
    }
  }

  // Decree the request if we're the proposer; otherwise just return.
  if (is_proposer()) {
    return proposer_decree_request(req);
  } else {
    return 0;
  }
}

/**
 * proposer_ack_request - Dispatch a request as a decree.
 */
int
proposer_ack_request(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_request *req;
  struct paxos_acceptor *acc;

  // Allocate a request and unpack into it.
  req = g_malloc0(sizeof(*req));
  paxos_request_unpack(req, o);

  // The requester overloads ph_inst to the ID of the acceptor it believes
  // to be the proposer.  If the requester has a live connection to us but
  // thinks that a lower-ranked acceptor is the proposer, kill them for
  // having inconsistent state.
  //
  // It is possible that a higher-ranked acceptor is identified as the
  // proposer.  This should occur only in the case that we are preparing but
  // have lost our connection to the true proposer.  If we indeed are not the
  // proposer, our prepare will fail, and we will be redirected at that point.
  if (hdr->ph_inum > pax.self_id) {
    acc = acceptor_find(&pax.alist, req->pr_val.pv_reqid.id);
    request_destroy(req);
    return proposer_decree_part(acc);
  }

  // Add it to the request cache if needed.
  if (request_needs_cached(req->pr_val.pv_dkind)) {
    request_insert(&pax.rcache, req);
  }

  return proposer_decree_request(req);
}

/**
 * acceptor_ack_request - Cache a requester's message, waiting for the
 * proposer to decree it.
 */
int
acceptor_ack_request(struct paxos_peer *source, struct paxos_header *hdr,
    msgpack_object *o)
{
  int r;
  struct paxos_request *req;

  // The requester overloads ph_inst to the acceptor it believes to be the
  // proposer.  If we are incorrectly identified as the proposer (i.e., if
  // we believe someone higher-ranked is still live), send a redirect.
  if (hdr->ph_inum == pax.self_id) {
    r = paxos_redirect(source, hdr);
  }

  // Allocate a request and unpack into it.
  req = g_malloc0(sizeof(*req));
  paxos_request_unpack(req, o);

  // Add it to the request cache.
  request_insert(&pax.rcache, req);

  return r;
}

/**
 * paxos_retrieve - Ask the originator of request data to send us data which
 * we do not have in our cache.
 *
 * We call this function when and only when we are issued a commit for an
 * instance whose associated request is not in our request cache.
 */
int
paxos_retrieve(struct paxos_instance *inst)
{
  int r;
  struct paxos_header hdr;
  struct paxos_acceptor *acc;
  struct paxos_yak py;

  // Initialize a header.
  hdr.ph_ballot.id = pax.ballot.id;
  hdr.ph_ballot.gen = pax.ballot.gen;
  hdr.ph_opcode = OP_RETRIEVE;
  hdr.ph_inum = inst->pi_hdr.ph_inum; // Instance number of the request.

  // Pack the retrieve.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, &hdr);
  paxos_payload_begin_array(&py, 2);
  paxos_paxid_pack(&py, pax.self_id);
  paxos_value_pack(&py, &inst->pi_val);

  // Determine the request originator and send.  If we are no longer connected
  // to the request originator, broadcast the retrieve instead.
  acc = acceptor_find(&pax.alist, inst->pi_val.pv_reqid.id);
  if (acc == NULL || acc->pa_peer == NULL) {
    r = paxos_broadcast(UNYAK(&py));
  } else {
    r = paxos_send(acc, UNYAK(&py));
  }
  paxos_payload_destroy(&py);

  return r;
}

/**
 * paxos_ack_retrieve - Acknowledge a retrieve.
 *
 * This basically just unpacks and wraps a resend.
 */
int paxos_ack_retrieve(struct paxos_header *hdr, msgpack_object *o)
{
  paxid_t paxid;
  msgpack_object *p;
  struct paxos_value val;
  struct paxos_request *req;
  struct paxos_acceptor *acc;

  // Make sure the payload is well-formed.
  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size == 2);
  p = o->via.array.ptr;

  // Unpack the retriever's ID and the value being retrieved.
  paxos_paxid_unpack(&paxid, p++);
  paxos_value_unpack(&val, p++);

  // Retrieve the request.
  assert(request_needs_cached(val.pv_dkind));
  req = request_find(&pax.rcache, val.pv_reqid);
  if (req != NULL) {
    // If we have the request, look up the recipient and resend.
    acc = acceptor_find(&pax.alist, paxid);
    return paxos_resend(acc, hdr, req);
  } else {
    // If we don't have the request either, just return.
    return 0;
  }
}

/**
 * paxos_resend - Resend request data that some acceptor didn't have at
 * commit time.
 */
int
paxos_resend(struct paxos_acceptor *acc, struct paxos_header *hdr,
    struct paxos_request *req)
{
  int r;
  struct paxos_yak py;

  // Modify the header.
  hdr->ph_opcode = OP_RESEND;

  // Just pack and send the resend.
  paxos_payload_init(&py, 2);
  paxos_header_pack(&py, hdr);
  paxos_request_pack(&py, req);
  r = paxos_send(acc, UNYAK(&py));
  paxos_payload_destroy(&py);

  return r;
}

/**
 * paxos_ack_resend - Receive a resend of request data, and re-commit the
 * instance to which the request belongs.
 */
int
paxos_ack_resend(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst;
  struct paxos_request *req;

  // Grab the instance for which we wanted the request.  If we find that it
  // has already been learned since we began our retrieval, we can just
  // return.
  //
  // Note also that an instance should only be NULL if it was committed and
  // learned and then truncated in a sync operation.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  if (inst == NULL || inst->pi_learned) {
    return 0;
  }

  // If we had already obtained the request, we would have committed, so
  // let's ensure that we haven't.
  req = request_find(&pax.rcache, inst->pi_val.pv_reqid);
  assert(req == NULL);

  // Allocate a request and unpack it.
  req = g_malloc0(sizeof(*req));
  paxos_request_unpack(req, o);

  // Insert it to our request cache.
  request_insert(&pax.rcache, req);

  // Commit again, now that we have the associated request.
  return paxos_commit(inst);
}
