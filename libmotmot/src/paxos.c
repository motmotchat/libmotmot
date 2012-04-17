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

static inline void
swap(void **p1, void **p2)
{
  void *tmp = *p1;
  *p1 = *p2;
  *p2 = tmp;
}

// Local protocol state
struct paxos_state pax;

// Paxos operations
void paxos_drop_connection(GIOChannel *);
// int paxos_request();

// Proposer operations
int proposer_prepare(GIOChannel *);
int proposer_decree(struct paxos_instance *);
int proposer_commit(struct paxos_instance *);
int proposer_ack_promise(struct paxos_header *, msgpack_object *);
int proposer_ack_accept(struct paxos_header *);
int proposer_ack_request(struct paxos_header *, msgpack_object *);

// Acceptor operations
int acceptor_ack_prepare(GIOChannel *, struct paxos_header *);
int acceptor_promise(struct paxos_header *);
int acceptor_ack_decree(struct paxos_header *, msgpack_object *);
int acceptor_accept(struct paxos_header *);
int acceptor_ack_commit(struct paxos_header *);
int acceptor_request(dkind_t, size_t, const char *);

int
proposer_dispatch(GIOChannel *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  // If the message is for some other ballot, send a redirect.
  // XXX: Not sure if we want to do this, say, for an OP_REQUEST.
  if (!ballot_compare(pax.ballot, hdr->ph_ballot)) {
    // TODO: paxos_redirect();
    return TRUE;
  }

  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      g_error("Bad request PREPARE recieved by proposer. Redirecting...");
      // TODO: paxos_redirect();
      break;
    case OP_PROMISE:
      proposer_ack_promise(hdr, o);
      break;

    case OP_DECREE:
      g_error("Bad request DECREE recieved by proposer. Redirecting...");
      // TODO: paxos_redirect();
      break;

    case OP_ACCEPT:
      proposer_ack_accept(hdr);
      break;

    case OP_COMMIT:
      // TODO: Commit and relinquish presidency if the ballot is higher,
      // otherwise check if we have a decree of the given instance.  If
      // we do and /are not preparing/, redirect; otherwise (if we don't
      // or we do but we are preparing), commit it.
      break;

    case OP_REQUEST:
      proposer_ack_request(hdr, o);
      break;

    case OP_REDIRECT:
      // TODO: Decide what to do.
      break;
  }

  return TRUE;
}

int
acceptor_dispatch(GIOChannel *source, struct paxos_header *hdr,
    struct msgpack_object *o)
{
  switch (hdr->ph_opcode) {
    case OP_PREPARE:
      acceptor_ack_prepare(source, hdr);
      break;

    case OP_PROMISE:
      g_error("Bad request PROMISE recieved by acceptor. Redirecting...");
      // TODO: paxos_redirect();
      break;

    case OP_DECREE:
      acceptor_ack_decree(hdr, o);
      break;

    case OP_ACCEPT:
      g_error("Bad request ACCEPT recieved by acceptor. Redirecting...");
      // TODO: paxos_redirect();
      break;

    case OP_COMMIT:
      acceptor_ack_commit(hdr);
      break;

    case OP_REQUEST:
      g_error("Bad request REQUEST recieved by acceptor. Redirecting...");
      // TODO: paxos_redirect();
      break;

    case OP_REDIRECT:
      // TODO: Think Real Hard (tm)
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
  struct paxos_header *hdr;

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
    g_warning("paxos_dispatch: Read from socket failed.\n");
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
  hdr = g_malloc0(sizeof(*hdr));
  paxos_header_unpack(hdr, p);

  // Switch on the type of message received.
  if (is_proposer()) {
    retval = proposer_dispatch(source, hdr, p + 1);
  } else {
    retval = acceptor_dispatch(source, hdr, p + 1);
  }

  msgpack_unpacked_destroy(&res);
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
  struct paxos_header hdr;
  struct paxos_yak py;
  paxid_t inum;

  // If we were already preparing, get rid of that prepare.
  // XXX: I don't think this is possible.
  if (pax.prep != NULL) {
    g_free(pax.prep);
  }

  // Start a new prepare and a new ballot.
  pax.prep = g_malloc0(sizeof(*pax.prep));
  pax.ballot.id = pax.self_id;
  pax.ballot.gen++;

  // Default to the next instance.
  pax.prep->pp_nacks = 1;
  pax.prep->pp_inum = next_instance();
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
  hdr.ph_ballot = pax.ballot;
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

  // XXX: Act on the decree (e.g., display chat, record configs).
  // We'll need to find the corresponding request in the request queue for
  // the actual data, but only for those dkinds that are associated with
  // requests.

  return 0;
}

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
proposer_ack_promise(struct paxos_header *hdr, msgpack_object *o)
{
  msgpack_object *p, *pend, *r;
  struct paxos_instance *inst, *it;
  paxid_t inum;
  struct paxos_yak py;

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
          ballot_compare(inst->pi_hdr.ph_ballot, it->pi_hdr.ph_ballot) > 1) {
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

  it = LIST_FIRST(&pax.ilist);

  // For each Paxos instance for which we don't have a commit, send a decree.
  for (inum = pax.prep->pp_nacks; ; ++inum) {
    // Get the closest instance with number >= inum.
    it = get_instance_lub(it, &pax.ilist, inum);

    // If we're at the end of the list, break.
    if (it == (void *)&pax.ilist) {
      break;
    }

    if (it->pi_hdr.ph_inum > inum) {
      // Nobody in the quorum (including ourselves) has heard of this instance,
      // so make a null decree.
      inst = g_malloc0(sizeof(*inst));

      inst->pi_hdr.ph_ballot = pax.ballot;
      inst->pi_hdr.ph_opcode = OP_DECREE;
      inst->pi_hdr.ph_inum = inum;

      inst->pi_votes = 1;

      inst->pi_val.pv_dkind = DEC_NULL;
      inst->pi_val.pv_srcid = pax.self_id;
      inst->pi_val.pv_reqid = pax.req_id;

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
    paxos_payload_init(&py, 2);
    paxos_header_pack(&py, &(inst->pi_hdr));
    paxos_value_pack(&py, &(inst->pi_val));
    paxos_broadcast(UNYAK(&py));
    paxos_payload_destroy(&py);
  }

  // Free the prepare and return.
  g_free(pax.prep);
  return 0;
}

/**
 * proposer_ack_accept - Acknowledge an acceptor's accept.
 *
 * Just increment the vote count of the correct Paxos instance and commit
 * if we have a majority.
 */
int
proposer_ack_accept(struct paxos_header *hdr)
{
  struct paxos_instance *inst;

  // Find the decree of the correct instance and increment the vote count.
  inst = instance_find(&pax.ilist, hdr->ph_inum);
  inst->pi_votes++;

  // If we have a majority, send a commit message.
  if (inst->pi_votes >= MAJORITY) {
    proposer_commit(inst);
  }

  return 0;
}

/**
 * proposer_ack_request - Dispatch a request as a decree.
 */
int
proposer_ack_request(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst;

  // Allocate an instance and unpack a value into it.
  inst = g_malloc0(sizeof(*inst));
  paxos_value_unpack(&inst->pi_val, o);

  // Send a decree.
  proposer_decree(inst);

  // XXX: We should decide how to pack the raw data into a request (perhaps
  // as a third array?) and then unpack it and add it to our request queue.

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
acceptor_ack_prepare(GIOChannel *source, struct paxos_header *hdr)
{
  if (pax.proposer->pa_chan != source) {
    // TODO: paxos_redirect();
    return 0;
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
 * where we pack the structs as two-element arrays.
 */
int
acceptor_promise(struct paxos_header *hdr)
{
  size_t count;
  struct paxos_instance *it;
  struct paxos_yak py;

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
acceptor_ack_decree(struct paxos_header *hdr, msgpack_object *o)
{
  struct paxos_instance *inst, *it;

  // Unpack the message value
  inst = g_malloc0(sizeof(*inst));
  memcpy(&inst->pi_hdr, hdr, sizeof(hdr));
  paxos_value_unpack(&inst->pi_val, o);
  inst->pi_votes = 2; // XXX: it's at least 2...

  // Look through the ilist backwards, since we expect to be mostly appending
  LIST_FOREACH_REV(it, &pax.ilist, pi_le) {
    if (it->pi_hdr.ph_inum < inst->pi_hdr.ph_inum) {
      // We've found an insertion point
      LIST_INSERT_AFTER(&pax.ilist, it, inst, pi_le);
      break;
    } else if (it->pi_hdr.ph_inum == inst->pi_hdr.ph_inum) {
      // We have a duplicate instance number. What's going on?!
      if (it->pi_votes > 0) {
        // We haven't learned the value yet, so we probably got overruled by
        // the new president's quorum
        memcpy(&it->pi_hdr, &inst->pi_hdr, sizeof(inst->pi_hdr));
        memcpy(&it->pi_val, &inst->pi_val, sizeof(inst->pi_val));
        it->pi_votes = 2;
        // Since we've just clobbered the old instance, free the one we made
        g_free(inst);
      } else {
        // We've already learned that value, so ignore the decree. Hopefully
        // it's the same as the one we already have, but let's not bother
        // checking for now (TODO?)
        g_free(inst);
        return 0;
      }
    }
  }

  // We didn't find it. Now we check (1) if the list is empty, or (2) if the
  // first element in the list has a larger instance number than us. Note
  // that neither of these conditions will be true if we broke out of the
  // previous loop, so we don't need to worry about weird edge cases
  it = LIST_FIRST(&pax.ilist);
  if (LIST_EMPTY(&pax.ilist) || it->pi_hdr.ph_inum > inst->pi_hdr.ph_inum) {
    LIST_INSERT_HEAD(&pax.ilist, inst, pi_le);
  }

  // By now we've inserted the element *somewhere* in the list, so accept it
  return acceptor_accept(hdr);
}

/**
 * acceptor_accept - Notify the proposer we accept their decree
 *
 * Respond to the proposer with an acknowledgement that we have committed their
 * decree.
 */
int
acceptor_accept(struct paxos_header *hdr)
{
  struct paxos_header header;
  struct paxos_yak py;

  memcpy(&header, hdr, sizeof(header));
  header.ph_opcode = OP_ACCEPT;

  paxos_payload_init(&py, 1);
  paxos_header_pack(&py, &header);

  paxos_send_to_proposer(UNYAK(&py));

  paxos_payload_destroy(&py);
  return 0;
}


/**
 * acceptor_ack_commit - Commit ("learn") a value
 *
 * Commit this value as a permanent learned value, and notify listeners of the
 * value payload.
 */
int
acceptor_ack_commit(struct paxos_header *hdr)
{
  struct paxos_instance *it;

  // Look through the ilist backwards, since we expect to be mostly appending
  LIST_FOREACH_REV(it, &pax.ilist, pi_le) {
    if (it->pi_hdr.ph_inum == hdr->ph_inum) {
      if (it->pi_hdr.ph_ballot.id  != hdr->ph_ballot.id ||
          it->pi_hdr.ph_ballot.gen != hdr->ph_ballot.gen) {
        // What's going on?!
        g_critical("acceptor_ack_commit: Mismatching ballot numbers");
        return 0;
      }

      // We use the sentinel value of 0 to indicate committed values
      it->pi_votes = 0;
      return 0;
    }
  }

  g_critical("acceptor_ack_commit: Cannot commit unknown instance");
  return 1;
}

/**
 * acceptor_request - Request the president to propose the given value
 */
int
acceptor_request(dkind_t kind, size_t len, const char *message)
{
  return 0;
}
