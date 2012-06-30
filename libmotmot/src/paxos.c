/**
 * paxos.c - Paxos protocol functions that live near the interface.
 */

#include <assert.h>
#include <glib.h>

#include "paxos.h"
#include "paxos_continue.h"
#include "paxos_io.h"
#include "paxos_msgpack.h"
#include "paxos_print.h"
#include "paxos_protocol.h"
#include "paxos_state.h"
#include "paxos_util.h"
#include "containers/list.h"

// Global system state.
struct paxos_state state;

// Current session.
struct paxos_session *pax;

int proposer_force_kill(struct paxos_peer *);

/**
 * paxos_init - Initialize local Paxos state.
 *
 * Most of our state is worthless until we are welcomed to the system.
 */
int
paxos_init(connect_t connect, struct learn_table *learn, enter_t enter,
    leave_t leave)
{
  state.connect = connect;
  state.enter = enter;
  state.leave = leave;
  state.learn.chat = learn->chat;
  state.learn.join = learn->join;
  state.learn.part = learn->part;

  LIST_INIT(&state.sessions);
  connect_hashinit();
  state.connections = connect_container_new();

  return 0;
}

/**
 * paxos_start - Start up the Paxos protocol with ourselves as the proposer.
 */
void *
paxos_start(const void *desc, size_t size, void *data)
{
  pax_uuid_t *uuid;
  struct paxos_request *req;
  struct paxos_instance *inst;
  struct paxos_acceptor *acc;

  // Create a new session with a fresh UUID.
  pax = session_new(data, 1);

  // Give ourselves ID 1.
  pax->self_id = 1;

  // Prepare a new ballot.  Hey, we accept the prepare!  Hoorah.
  pax->ballot.id = pax->self_id;
  pax->ballot.gen = 1;
  pax->gen_high = 1;

  // Submit a join request to the cache.
  req = g_malloc0(sizeof(*req));

  req->pr_val.pv_dkind = DEC_JOIN;
  req->pr_val.pv_reqid.id = pax->self_id;
  req->pr_val.pv_reqid.gen = (++pax->req_id);

  req->pr_size = size;
  req->pr_data = g_memdup(desc, size);

  LIST_INSERT_TAIL(&pax->rcache, req, pr_le);

  // Artificially generate an initial commit, without learning.
  inst = g_malloc0(sizeof(*inst));
  header_init(&inst->pi_hdr, OP_DECREE, 1);

  inst->pi_committed = true;
  inst->pi_cached = true;
  inst->pi_learned = true;

  inst->pi_votes = 1;
  inst->pi_rejects = 0;

  memcpy(&inst->pi_val, &req->pr_val, sizeof(req->pr_val));

  // Initialize the ilist.
  LIST_INSERT_HEAD(&pax->ilist, inst, pi_le);
  pax->ibase = 1;

  // Set up the learn protocol parameters to start at the next instance.
  pax->ihole = 2;
  pax->istart = inst;

  // Add ourselves to the acceptor list.
  acc = g_malloc0(sizeof(*acc));
  acc->pa_paxid = pax->self_id;
  acc->pa_peer = NULL;
  acc->pa_size = size;
  acc->pa_desc = g_memdup(desc, size);

  LIST_INSERT_HEAD(&pax->alist, acc, pa_le);
  pax->live_count = 1;

  // Set ourselves as the proposer.
  pax->proposer = acc;

  // Add a sync for this session.
  uuid = g_malloc0(sizeof(*uuid));
  *uuid = *pax->session_id;
  g_timeout_add_seconds(1, paxos_sync, uuid);

  return pax;
}

/**
 * paxos_end - End our participancy in a Paxos protocol.
 */
int
paxos_end(void *session)
{
  pax = (struct paxos_session *)session;

  // Destroy the session.
  LIST_REMOVE(&state.sessions, pax, session_le);
  session_destroy(pax);

  // Tell the client that the session is ending.  The client must promise us
  // that no more calls into Paxos will be made for the terminating session.
  state.leave(pax->client_data);

  return 1;
}

/**
 * paxos_register_connection - Register a channel with Paxos.
 *
 * This function is called whenever somebody tries to connect to us.
 */
int
paxos_register_connection(GIOChannel *chan)
{
  // Just initialize a peer object.
  return paxos_peer_init(chan) == NULL;
}

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
int
paxos_drop_connection(struct paxos_peer *source)
{
  int r = 0, found;
  struct paxos_acceptor *acc;

  // Process the drop for every session.
  // XXX: Have a single global list of connections.
  LIST_FOREACH(pax, &state.sessions, session_le) {
    found = false;

    // If the acceptor is participating in this session, mark it as dead.
    LIST_FOREACH(acc, &pax->alist, pa_le) {
      if (acc->pa_peer == source) {
        paxos_peer_destroy(acc->pa_peer);
        acc->pa_peer = NULL;
        pax->live_count--;
        found = true;
        break;
      }
    }

    if (!found) {
      continue;
    }

    if (is_proposer()) {
      // If we are the proposer, decree a part for the acceptor.
      ERR_ACCUM(r, proposer_decree_part(acc, 0));
    } else if (acc->pa_paxid == pax->proposer->pa_paxid) {
      // Otherwise, check if we lost the proposer.  If so, we "elect" the new
      // proposer, and if it's ourselves, we send a prepare.
      reset_proposer();
      if (is_proposer()) {
        ERR_ACCUM(r, proposer_prepare(acc));
      }
    }
  }

  return r;
}

/**
 * proposer_dispatch - Process a message as the (self-determined) proposer.
 */
static int
proposer_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int r = 0;

#ifdef DEBUG
  paxos_header_print(hdr, "P: ", "\n");
#endif

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_PROMISE:
      r = proposer_ack_promise(hdr, o);
      break;
    case OP_DECREE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_ACCEPT:
      r = proposer_ack_accept(hdr);
      break;
    case OP_COMMIT:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;

    case OP_REQUEST:
      r = proposer_ack_request(hdr, o);
      break;
    case OP_RETRIEVE:
      r = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      r = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_HELLO:
      r = paxos_ack_hello(source, hdr);
      break;

    case OP_REDIRECT:
      r = proposer_ack_redirect(hdr, o);
      break;
    case OP_REFUSE:
      // If an acceptor refuses our request, we identify them as higher-ranked
      // than we are, even if they are not the proposer.  If we are the proposer
      // now, the sending acceptor must have died.  This is an invalid system
      // state; kill the offender.
      r = proposer_force_kill(source);
    case OP_REJECT:
      r = proposer_ack_reject(hdr);
      break;

    case OP_RETRY:
      r = proposer_ack_retry(hdr);
      break;
    case OP_RECOMMIT:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;

    case OP_SYNC:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
    case OP_LAST:
      r = proposer_ack_last(hdr, o);
      break;
    case OP_TRUNCATE:
      // Invalid system state; kill the offender.
      r = proposer_force_kill(source);
      break;
  }

  return r;
}

/**
 * acceptor_dispatch - Process a message as an acceptor.
 */
static int
acceptor_dispatch(struct paxos_peer *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  int r = 0;

#ifdef DEBUG
  paxos_header_print(hdr, "A: ", "\n");
#endif

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      r = acceptor_ack_prepare(source, hdr);
      break;
    case OP_PROMISE:
      // Ignore promises.
      break;
    case OP_DECREE:
      r = acceptor_ack_decree(hdr, o);
      break;
    case OP_ACCEPT:
      // Ignore accepts.
      break;
    case OP_COMMIT:
      r = acceptor_ack_commit(hdr, o);
      break;

    case OP_REQUEST:
      r = acceptor_ack_request(source, hdr, o);
      break;
    case OP_RETRIEVE:
      r = paxos_ack_retrieve(hdr, o);
      break;
    case OP_RESEND:
      r = paxos_ack_resend(hdr, o);
      break;

    case OP_WELCOME:
      // Ignore welcomes; they should be handled in paxos_dispatch().
      break;
    case OP_HELLO:
      r = paxos_ack_hello(source, hdr);
      break;

    case OP_REDIRECT:
      // Ignore redirects.
      break;
    case OP_REFUSE:
      r = acceptor_ack_refuse(hdr, o);
      break;
    case OP_REJECT:
      // Ignore rejects.
      break;

    case OP_RETRY:
      // Ignore retries.
      break;
    case OP_RECOMMIT:
      r = acceptor_ack_recommit(hdr, o);
      break;

    case OP_SYNC:
      r = acceptor_ack_sync(hdr);
      break;
    case OP_LAST:
      // Ignore lasts.
      break;
    case OP_TRUNCATE:
      r = acceptor_ack_truncate(hdr, o);
      break;
  }

  return 0;
}

/**
 * paxos_dispatch - Handle a Paxos message.
 */
int
paxos_dispatch(struct paxos_peer *source, const msgpack_object *o)
{
  int r;
  struct paxos_header *hdr;

  assert(o->type == MSGPACK_OBJECT_ARRAY);
  assert(o->via.array.size > 0 && o->via.array.size <= 2);

  // Unpack the Paxos header.  This may be clobbered by the proposer/acceptor
  // routines which the dispatch functions call.
  hdr = g_malloc0(sizeof(*hdr));
  paxos_header_unpack(hdr, o->via.array.ptr);

  // Bind `pax` to the session identified in the message header.
  pax = session_find(&state.sessions, &hdr->ph_session);
  if (pax == NULL) {
    // If we have no session, wait for a welcome message.
    if (hdr->ph_opcode == OP_WELCOME) {
      r = acceptor_ack_welcome(source, hdr, o->via.array.ptr + 1);
    } else {
      r = 0;
    }
  } else {
    // Switch on the type of message received.
    if (is_proposer()) {
      r = proposer_dispatch(source, hdr, o->via.array.ptr + 1);
    } else {
      r = acceptor_dispatch(source, hdr, o->via.array.ptr + 1);
    }
  }

  g_free(hdr);
  return r;
}

/**
 * proposer_force_kill - Somebody is misbehaving in our Paxos, and our proposer
 * will have none of it.
 *
 * We call this whenever the proposer receives a message from someone else who
 * thinks they have the right to propose.  Since the ranking for proposer
 * eligibility is the result of Paxos commits, and since we totally order all
 * learns of commits, this qualifies as an inconsistent state of the system,
 * which should not actually be possible by Paxos's correctness guarantees.
 *
 * To make a best effort at eliminating the inconsistency, we call this routine
 * to try to kill the offender.
 */
int
proposer_force_kill(struct paxos_peer *source)
{
  struct paxos_acceptor *acc;

  // Cry.
  g_critical("paxos_dispatch: Two live proposers detected.");

  // Find our acceptor object.
  LIST_FOREACH(acc, &pax->alist, pa_le) {
    if (acc->pa_peer == source) {
      // Decree a kill for the acceptor.
      return proposer_decree_part(acc, 1);
    }
  }

  // If we didn't find the acceptor, fail.
  return 1;
}
