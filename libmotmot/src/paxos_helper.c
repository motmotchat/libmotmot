/**
 * paxos_helper.c - Helper functions for Paxos.
 */
#include "paxos.h"
#include "paxos_helper.h"
#include "paxos_io.h"
#include "list.h"

#include <fcntl.h>
#include <unistd.h>
#include <glib.h>


///////////////////////////////////////////////////////////////////////////
//
//  Comparison functions.
//

/**
 * Compare two paxid's.
 */
int
paxid_compare(paxid_t x, paxid_t y)
{
  if (x < y) {
    return -1;
  } else if (x > y) {
    return 1;
  } else {
    return 0;
  }
}

/**
 * Compare two paxid pairs.
 *
 * The first coordinate is subordinate to the second.  The second compares
 * normally (x.gen < y.gen ==> -1) but the first compares inversely (x.id
 * < y.id ==> 1)
 */
int
ppair_compare(ppair_t x, ppair_t y)
{
  // Compare generation first.
  if (x.gen < y.gen) {
    return -1;
  } else if (x.gen > y.gen) {
    return 1;
  }

  /* x.gen == y.gen */
  if (x.id < y.id) {
    return 1;
  } else if (x.id > y.id) {
    return -1;
  } else /* x.id == y.id */  {
    return 0;
  }
}

int
ballot_compare(ballot_t x, ballot_t y)
{
  return ppair_compare(x, y);
}

int
reqid_compare(reqid_t x, reqid_t y)
{
  return ppair_compare(x, y);
}


///////////////////////////////////////////////////////////////////////////
//
//  Initializer routines.
//

struct paxos_session *
session_new(void *data, int gen_uuid)
{
  struct paxos_session *session;

  session = g_malloc0(sizeof(*session));
  session_insert(&state.sessions, session);

  // Generate a UUID and set client data pointer if desired.
  if (gen_uuid) {
    pax_uuid_gen(&session->session_id);
  }
  session->client_data = data;

  // Initialize all our lists.
  LIST_INIT(&session->alist);
  LIST_INIT(&session->adefer);
  LIST_INIT(&session->clist);
  LIST_INIT(&session->ilist);
  LIST_INIT(&session->idefer);
  LIST_INIT(&session->rcache);

  return session;
}

void
header_init(struct paxos_header *hdr, paxop_t opcode, paxid_t inum)
{
  hdr->ph_session = pax->session_id;
  hdr->ph_ballot.id = pax->ballot.id;
  hdr->ph_ballot.gen = pax->ballot.gen;
  hdr->ph_opcode = opcode;
  hdr->ph_inum = inum;
}

void
instance_init_metadata(struct paxos_instance *inst)
{
  inst->pi_committed = false;
  inst->pi_cached = false;
  inst->pi_learned = false;
  inst->pi_votes = 1;
  inst->pi_rejects = 0;
}

struct paxos_continuation *
continuation_new(motmot_connect_continuation_t func, paxid_t paxid)
{
  struct paxos_continuation *conn;

  conn = g_malloc0(sizeof(*conn));
  conn->pc_cb.func = func;
  conn->pc_cb.data = conn;
  conn->pc_session_id = pax->session_id;
  conn->pc_paxid = paxid;
  conn->pc_inum = 0;

  LIST_INSERT_TAIL(&pax->clist, conn, pc_le);

  return conn;
}


///////////////////////////////////////////////////////////////////////////
//
//  Destructor routines.
//

void
acceptor_destroy(struct paxos_acceptor *acc)
{
  if (acc != NULL) {
    paxos_peer_destroy(acc->pa_peer);
    g_free(acc->pa_desc);
  }
  g_free(acc);
}

void
instance_destroy(struct paxos_instance *inst)
{
  g_free(inst);
}

void
request_destroy(struct paxos_request *req)
{
  if (req != NULL) {
    g_free(req->pr_data);
  }
  g_free(req);
}

void
continuation_destroy(struct paxos_continuation *conn)
{
  g_free(conn);
}

void
session_destroy(struct paxos_session *session)
{
  // Wipe all our lists.
  acceptor_list_destroy(&pax->alist);
  acceptor_list_destroy(&pax->adefer);
  continuation_list_destroy(&pax->clist);
  instance_list_destroy(&pax->ilist);
  instance_list_destroy(&pax->idefer);
  request_list_destroy(&pax->rcache);

  g_free(session);
}


///////////////////////////////////////////////////////////////////////////
//
//  List operations.
//

#define XLIST_FIND_IMPL(name, id_t, le_field, id_field, compare)      \
  struct paxos_##name *                                               \
  name##_find(struct name##_list *head, id_t id)                      \
  {                                                                   \
    int cmp;                                                          \
    struct paxos_##name *it;                                          \
                                                                      \
    if (LIST_EMPTY(head)) {                                           \
      return NULL;                                                    \
    }                                                                 \
                                                                      \
    LIST_FOREACH(it, head, le_field) {                                \
      cmp = compare(id, it->id_field);                                \
      if (cmp == 0) {                                                 \
        return it;                                                    \
      } else if (cmp < 0) {                                           \
        break;                                                        \
      }                                                               \
    }                                                                 \
                                                                      \
    return NULL;                                                      \
  }

#define XLIST_FIND_REV_IMPL(name, id_t, le_field, id_field, compare)  \
  struct paxos_##name *                                               \
  name##_find(struct name##_list *head, id_t id)                      \
  {                                                                   \
    int cmp;                                                          \
    struct paxos_##name *it;                                          \
                                                                      \
    if (LIST_EMPTY(head)) {                                           \
      return NULL;                                                    \
    }                                                                 \
                                                                      \
    LIST_FOREACH_REV(it, head, le_field) {                            \
      cmp = compare(id, it->id_field);                                \
      if (cmp == 0) {                                                 \
        return it;                                                    \
      } else if (cmp > 0) {                                           \
        break;                                                        \
      }                                                               \
    }                                                                 \
                                                                      \
    return NULL;                                                      \
  }

#define XLIST_INSERT_IMPL(name, le_field, id_field, compare)          \
  struct paxos_##name *                                               \
  name##_insert(struct name##_list *head, struct paxos_##name *elt)   \
  {                                                                   \
    int cmp;                                                          \
    struct paxos_##name *it;                                          \
                                                                      \
    LIST_FOREACH(it, head, le_field) {                                \
      cmp = compare(elt->id_field, it->id_field);                     \
      if (cmp == 0) {                                                 \
        return it;                                                    \
      } else if (cmp < 0) {                                           \
        LIST_INSERT_BEFORE(head, it, elt, le_field);                  \
        return elt;                                                   \
      }                                                               \
    }                                                                 \
                                                                      \
    LIST_INSERT_TAIL(head, elt, le_field);                            \
    return elt;                                                       \
  }

#define XLIST_INSERT_REV_IMPL(name, le_field, id_field, compare)      \
  struct paxos_##name *                                               \
  name##_insert(struct name##_list *head, struct paxos_##name *elt)   \
  {                                                                   \
    int cmp;                                                          \
    struct paxos_##name *it;                                          \
                                                                      \
    LIST_FOREACH_REV(it, head, le_field) {                            \
      cmp = compare(elt->id_field, it->id_field);                     \
      if (cmp == 0) {                                                 \
        return it;                                                    \
      } else if (cmp > 0) {                                           \
        LIST_INSERT_AFTER(head, it, elt, le_field);                   \
        return elt;                                                   \
      }                                                               \
    }                                                                 \
                                                                      \
    LIST_INSERT_HEAD(head, elt, le_field);                            \
    return elt;                                                       \
  }

#define XLIST_DESTROY_IMPL(name, le_field, destroy)                   \
  void                                                                \
  name##_list_destroy(struct name##_list *head)                       \
  {                                                                   \
    struct paxos_##name *it;                                          \
                                                                      \
    LIST_WHILE_FIRST(it, head) {                                      \
      LIST_REMOVE(head, it, le_field);                                \
      destroy(it);                                                    \
    }                                                                 \
  }

XLIST_FIND_IMPL(acceptor, paxid_t, pa_le, pa_paxid, paxid_compare);
XLIST_FIND_REV_IMPL(instance, paxid_t, pi_le, pi_hdr.ph_inum, paxid_compare);
XLIST_FIND_IMPL(request, reqid_t, pr_le, pr_val.pv_reqid, reqid_compare);
XLIST_FIND_IMPL(session, pax_uuid_t, session_le, session_id, pax_uuid_compare);

XLIST_INSERT_REV_IMPL(acceptor, pa_le, pa_paxid, paxid_compare);
XLIST_INSERT_REV_IMPL(instance, pi_le, pi_hdr.ph_inum, paxid_compare);
XLIST_INSERT_REV_IMPL(request, pr_le, pr_val.pv_reqid, reqid_compare);
XLIST_INSERT_IMPL(session, session_le, session_id, pax_uuid_compare);

XLIST_DESTROY_IMPL(acceptor, pa_le, acceptor_destroy);
XLIST_DESTROY_IMPL(instance, pi_le, instance_destroy);
XLIST_DESTROY_IMPL(request, pr_le, request_destroy);
XLIST_DESTROY_IMPL(continuation, pc_le, continuation_destroy);
XLIST_DESTROY_IMPL(session, session_le, session_destroy);


///////////////////////////////////////////////////////////////////////////
//
//  UUID helpers
//

void
pax_uuid_gen(pax_uuid_t *uuid)
{
  int urandom = open("/dev/urandom", O_RDONLY);
  read(urandom, uuid, sizeof(*uuid));
  close(urandom);
}

void
pax_uuid_destroy(pax_uuid_t *uuid)
{
  (void)uuid;
  return;
}

int
pax_uuid_compare(pax_uuid_t x, pax_uuid_t y)
{
  if (x < y) {
    return -1;
  } else if (x > y) {
    return 1;
  } else {
    return 0;
  }
}


///////////////////////////////////////////////////////////////////////////
//
//  Message delivery wrappers
//

/**
 * Broadcast a message to all acceptors.
 */
int
paxos_broadcast(const char *buffer, size_t length)
{
  int r;
  struct paxos_acceptor *acc;

  LIST_FOREACH(acc, &(pax->alist), pa_le) {
    if (acc->pa_peer == NULL) {
      continue;
    }

    if ((r = paxos_peer_send(acc->pa_peer, buffer, length))) {
      return r;
    }
  }

  return 0;
}

/**
 * Send a message to any acceptor.
 */
int
paxos_send(struct paxos_acceptor *acc, const char *buffer, size_t length)
{
  return paxos_peer_send(acc->pa_peer, buffer, length);
}

/**
 * Send a message to the proposer.
 */
int
paxos_send_to_proposer(const char *buffer, size_t length)
{
  if (pax->proposer == NULL) {
    return 1;
  }

  return paxos_peer_send(pax->proposer->pa_peer, buffer, length);
}
