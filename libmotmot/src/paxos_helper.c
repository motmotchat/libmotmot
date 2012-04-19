/**
 * paxos_helper.c - Helper functions for Paxos.
 */
#include "paxos.h"
#include "list.h"

#include <stdio.h>
#include <glib.h>

///////////////////////////////////////////////////////////////////////////
//
//  Paxos state helpers
//

/*
 * Check if we think we are the proposer.
 */
int
is_proposer()
{
  return pax.self_id == pax.proposer->pa_paxid;
}

/*
 * Gets the next free instance number.
 */
paxid_t
next_instance()
{
  return LIST_EMPTY(&pax.ilist) ? 1 : LIST_LAST(&pax.ilist)->pi_hdr.ph_inum + 1;
}

/*
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

/*
 * Compare two paxid pairs.
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
    return -1;
  } else if (x.id > y.id) {
    return 1;
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

/*
 * Convenience function for denoting which dkinds are requests.
 */
int
is_request(dkind_t dkind)
{
  return (dkind == DEC_CHAT || dkind == DEC_JOIN || dkind == DEC_PART);
}


///////////////////////////////////////////////////////////////////////////
//
//  Specific list operations.
//

#define XLIST_FIND_IMPL(name, id_t, le_field, id_field, compare)      \
  struct paxos_##name *                                               \
  name##_find(struct name##_list *head, id_t id)                      \
  {                                                                   \
    int cmp;                                                          \
    struct paxos_##name *it;                                          \
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
    struct paxos_##name *it;                                            \
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

XLIST_FIND_IMPL(acceptor, paxid_t, pa_le, pa_paxid, paxid_compare);
XLIST_FIND_REV_IMPL(instance, paxid_t, pi_le, pi_hdr.ph_inum, paxid_compare);
XLIST_FIND_IMPL(request, reqid_t, pr_le, pr_val.pv_reqid, reqid_compare);

XLIST_INSERT_REV_IMPL(acceptor, pa_le, pa_paxid, paxid_compare);
XLIST_INSERT_REV_IMPL(instance, pi_le, pi_hdr.ph_inum, paxid_compare);
XLIST_INSERT_REV_IMPL(request, pr_le, pr_val.pv_reqid, reqid_compare);


///////////////////////////////////////////////////////////////////////////
//
//  Acceptor list operations
//

/**
 * Broadcast a message to all acceptors.
 */
int
paxos_broadcast(const char *buffer, size_t length)
{
  struct paxos_acceptor *acc;
  int retval;

  LIST_FOREACH(acc, &(pax.alist), pa_le) {
    if (acc->pa_peer == NULL) {
      continue;
    }

    retval = paxos_peer_send(acc->pa_peer, buffer, length);
    if (retval) {
      return retval;
    }
  }

  return 0;
}

/**
 * Send a message to the proposer
 */
int
paxos_send_to_proposer(const char *buffer, size_t length)
{
  if (pax.proposer == NULL) {
    return 1;
  }

  return paxos_peer_send(pax.proposer->pa_peer, buffer, length);
}
