/**
 * session.c - Utilities for managing Paxos sessions.
 */

#include <glib.h>

#include "paxos_state.h"
#include "containers/list_factory.h"
#include "types/session.h"

struct paxos_session *
session_new(void *data, int gen_uuid)
{
  struct paxos_session *session;

  session = g_malloc0(sizeof(*session));

  // Generate a UUID and set client data pointer if desired.
  if (gen_uuid) {
    pax_uuid_gen(session->session_id);
  }
  session->client_data = data;

  // Insert into the sessions list.
  session_insert(&state.sessions, session);

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
session_destroy(struct paxos_session *session)
{
  // Wipe all our lists.
  acceptor_container_destroy(&pax->alist);
  acceptor_container_destroy(&pax->adefer);
  continuation_container_destroy(&pax->clist);
  instance_container_destroy(&pax->ilist);
  instance_container_destroy(&pax->idefer);
  request_container_destroy(&pax->rcache);

  g_free(session);
}

LIST_IMPLEMENT(session, pax_uuid_t *, session_le, session_id, pax_uuid_compare,
    session_destroy, _FWD, _FWD);
