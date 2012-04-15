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

#define MPBUFSIZE 4096

// Local protocol state
struct paxos_state pax;

// Paxos operations
void paxos_drop_connection(GIOChannel *);

// Proposer operations
int proposer_prepare(GIOChannel *);
int proposer_decree(struct paxos_instance *);
int proposer_commit(struct paxos_instance *);
int proposer_ack_promise(struct paxos_hdr *, msgpack_object *);
int proposer_ack_accept(struct paxos_hdr *);
int proposer_ack_request(struct paxos_hdr *, msgpack_object *);

// Acceptor operations

int
proposer_dispatch(GIOChannel *source, struct paxos_hdr *hdr,
    struct msgpack_object *o)
{
  // If the message is for some other ballot, send a redirect.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    // TODO: paxos_redirect();
    return TRUE;
  }

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      // TODO: paxos_redirect();
      break;
    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;

    case OP_DECREE:
      // TODO: paxos_redirect();
      break;

    case OP_ACCEPT:
      proposer_ack_accept(hdr);
      break;

    case OP_COMMIT:
      // TODO: Commit and relinquish presidency if the ballot is higher,
      // otherwise check if we have a decree of the given instance.  If
      // we do, redirect; otherwise commit it.
      break;

    case OP_REQUEST:
      // TODO: Make a decree.
      break;

    case OP_REDIRECT:
      // TODO: Decide what to do.
      break;
  }

  return TRUE;
}

int
acceptor_dispatch(GIOChannel *source, struct paxos_hdr *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      break;

    case OP_PROMISE:
      break;

    case OP_DECREE:
      break;

    case OP_ACCEPT:
      break;

    case OP_COMMIT:
      break;

    case OP_REQUEST:
      break;

    case OP_REDIRECT:
      break;
  }

  return TRUE;
}

/*
 * Handle a Paxos message.
 *
 * XXX: We should probably separate the protocol work from the buffer motion.
 */
int
paxos_dispatch(GIOChannel *source, GIOCondition condition, void *data)
{
  int retval;
  struct paxos_hdr *hdr;

  msgpack_unpacker *pac;
  unsigned long len;
  msgpack_unpacked res;
  msgpack_object o, *p;

  GIOStatus status;
  GError *gerr = NULL;

  // Prep the msgpack stream.
  pac = (msgpack_unpacker *)data;
  msgpack_unpacker_reserve_buffer(pac, MPBUFSIZE);

  // Read up to MPBUFSIZE bytes into the stream.
  status = g_io_channel_read_chars(source, msgpack_unpacker_buffer(pac),
                                   MPBUFSIZE, &len, &gerr);

  if (status == G_IO_STATUS_ERROR) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Read from socket failed.\n");
  } else if (status == G_IO_STATUS_EOF) {
    paxos_drop_connection(source);
    return FALSE;
  }

  msgpack_unpacker_buffer_consumed(pac, len);

  // Pop a single Paxos payload.
  msgpack_unpacked_init(&res);
  msgpack_unpacker_next(pac, &res);
  o = res.data;

  // TODO: error handling?
  assert(o.type == MSGPACK_OBJECT_ARRAY && o.via.array.size > 0 &&
      o.via.array.size <= 2);

  p = o.via.array.ptr;

  // Unpack the Paxos header.
  hdr = g_malloc(sizeof(struct paxos_hdr));
  if (hdr == NULL) {
    // TODO: error handling
    dprintf(2, "paxos_dispatch: Could not allocate header.\n");
  }
  paxos_hdr_unpack(hdr, p);

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, p + 1);
  } else {
    retval = acceptor_dispatch(source, hdr, p + 1);
  }

  // TODO: freeing the msgpack_object

  g_free(hdr);
  return retval;
}

/**
 * paxos_drop_connection - Account for a lost connection.
 *
 * We mark the acceptor as unavailable, "elect" the new president locally,
 * and start a prepare phase if necessary.
 */
void
paxos_drop_connection(GIOChannel *source)
{
  struct paxos_acceptor *it;

  // Connection dropped; mark the acceptor as dead.
  LIST_FOREACH(it, &pax.alist, pa_le) {
    if (it->pa_chan == source) {
      it->pa_chan = NULL;
      break;
    }
  }

  // Oh noes!  Did we lose the proposer?
  if (it->pa_paxid == pax.proposer->pa_paxid) {
    // Let's mark the new one.
    LIST_FOREACH(it, &pax.alist, pa_le) {
      if (it->pa_chan != NULL) {
        pax.proposer = it;
        break;
      }
    }

    // If we're the new proposer, send a prepare.
    if (is_proposer()) {
      proposer_prepare(source);
    }
  }

  // Close the channel socket.
  close(g_io_channel_unix_get_fd(source));
}

static int
pack_and_broadcast_header(struct paxos_instance *inst, paxop_t opcode)
{
  struct paxos_hdr hdr;
  struct paxos_yak py;

  hdr.ph_ballot = pax.ballot;
  hdr.ph_opcode = opcode;
  hdr.ph_seqn = inst ? inst->pi_hdr.ph_seqn : next_instance();

  paxos_payload_new(&py, 1);
  paxos_hdr_pack(&py, &hdr);
  paxos_broadcast(paxos_payload_data(&py), paxos_payload_size(&py));

  return 0;
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
proposer_prepare(GIOChannel *source)
{
  struct paxos_instance *it;

  // If we were already preparing, get rid of that prepare.
  // XXX: I don't think this is possible.
  if (pax.prep != NULL) {
    g_free(pax.prep);
  }

  // Start a new prepare and a new ballot.
  pax.prep = g_malloc(sizeof(struct paxos_prep));
  if (pax.prep == NULL) {
    // TODO: error handling
    return -1;
  }
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Identify our first uncommitted instance.
  LIST_FOREACH(it, &pax.ilist, pi_le) {
    if (it->pi_votes != 0) {
      break;
    }
  }
  if (it == (void *)(&pax.ilist)) {
    it = NULL;
  }

  // Broadcast the prepare.
  pack_and_broadcast_header(it, OP_PREPARE);

  return 0;
}

int
proposer_decree(struct paxos_instance *inst)
{
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
  // Broadcast the commit.
  pack_and_broadcast_header(inst, OP_COMMIT);

  // Mark the instance committed.
  inst->pi_votes = 0;

  return 0;
}

/**
 * proposer_ack_promise - Acknowledge an acceptor's promise.
 *
 * We acknowledge the promise by incrementing the number of acceptors who
 * have responded to the prepare, and by accounting for the acceptor's
 * votes on those decrees for which we indicated we did not have commit
 * information.
 *
 * If we attain a majority of promises, we make decrees for all those
 * instances in which any acceptor voted, as well as null decrees for
 * any holes.  We then end the prepare.
 */
int
proposer_ack_promise(struct paxos_hdr *hdr, msgpack_object *o)
{
  paxid_t acc_id;
  msgpack_object *p, *pend, *r;
  struct paxos_instance *it, *inst;
  struct instance_list *prep_ilist;

  // Grab the acceptor's ID.
  p = o->via.array.ptr;
  acc_id = p[0].via.u64;

  // Initialize loop variables.
  pend = p[1].via.array.ptr + p[1].via.array.size;
  p = p[1].via.array.ptr;
  prep_ilist = &(pax.prep->pp_ilist);
  it = LIST_FIRST(prep_ilist);

  // Loop through all the past vote information.
  for (; p != pend; ++p) {
    r = p->via.array.ptr;

    // Extract a instance.
    inst = g_malloc(sizeof(struct paxos_instance));
    if (inst == NULL) {
      // TODO: cry
    }
    paxos_hdr_unpack(&inst->pi_hdr, r);
    paxos_val_unpack(&inst->pi_val, r + 1);

    // Find a decree for the same Paxos instance if one exists in the prep
    // list; otherwise, get the next highest instance.
    for (; it != (void *)prep_ilist; it = it->pi_le.le_next) {
      if (inst->pi_hdr.ph_seqn <= it->pi_hdr.ph_seqn) {
        break;
      }
    }

    // If the instance was not found, insert it at the tail or before the
    // iterator as necessary.  If it was found, compare the two, keeping or
    // inserting the larger-balloted decree in the list and freeing the other.
    if (it == (void *)prep_ilist) {
      LIST_INSERT_TAIL(prep_ilist, inst, pi_le);
    } else if (inst->pi_hdr.ph_seqn < it->pi_hdr.ph_seqn) {
      LIST_INSERT_BEFORE(prep_ilist, it, inst, pi_le);
    } else /* inst->pi_hdr.ph_seqn == it->pi_hdr.ph_seqn */ {
      if (ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 1) {
        LIST_INSERT_BEFORE(prep_ilist, it, inst, pi_le);
        LIST_REMOVE(prep_ilist, it, pi_le);
        instance_free(it);
      } else {
        instance_free(inst);
      }
    }
  }

  // Acknowledge the prep.
  pax.prep->pp_nacks++;

  // Return if we don't have a majority of acks; otherwise, end the prepare.
  if (pax.prep->pp_nacks < MAJORITY) {
    return 0;
  }

  // TODO: Process the list.

  // Free the prepare and return.
  g_free(pax.prep);
  return 0;
}

/**
 * proposer_ack_accept - Acknowedge an acceptor's accept.
 *
 * Just increment the vote count of the correct Paxos instance and commit
 * if we have a majority.
 */
int
proposer_ack_accept(struct paxos_hdr *hdr)
{
  struct paxos_instance *inst;

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax.ilist, hdr->ph_seqn);
  inst->pi_votes++;

  // If we have a majority, send a commit message.
  if (inst->pi_votes >= MAJORITY) {
    proposer_commit(inst);
  }

  return 0;
}

int
proposer_ack_request(struct paxos_hdr *hdr, msgpack_object *o)
{
  return 0;
}
