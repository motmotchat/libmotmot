/**
 * paxos_print.c - Paxos object printers.
 */
#include "paxos.h"
#include "paxos_print.h"

#include <stdio.h>

void
paxid_print(paxid_t paxid, const char *lead, const char *trail)
{
  printf("%s", lead);
  printf("%u", paxid);
  printf("%s", trail);
}

void
ppair_print(ppair_t pp, const char *lead, const char *trail)
{
  printf("%s", lead);
  paxid_print(pp.id, "(", ", ");
  paxid_print(pp.gen, "", ")");
  printf("%s", trail);
}

void
paxop_print(paxop_t op, const char *lead, const char *trail)
{
  printf("%s", lead);
  switch (op) {
    case OP_PREPARE:
      printf("OP_PREPARE ");
      break;
    case OP_PROMISE:
      printf("OP_PROMISE ");
      break;
    case OP_DECREE:
      printf("OP_DECREE  ");
      break;
    case OP_ACCEPT:
      printf("OP_ACCEPT  ");
      break;
    case OP_COMMIT:
      printf("OP_COMMIT  ");
      break;
    case OP_WELCOME:
      printf("OP_WELCOME ");
      break;
    case OP_HELLO:
      printf("OP_HELLO   ");
      break;
    case OP_REQUEST:
      printf("OP_REQUEST ");
      break;
    case OP_RETRIEVE:
      printf("OP_RETRIEVE");
      break;
    case OP_RESEND:
      printf("OP_RESEND  ");
      break;
    case OP_REDIRECT:
      printf("OP_REDIRECT");
      break;
    case OP_REJECT:
      printf("OP_REJECT  ");
      break;
    case OP_RETRY:
      printf("OP_RETRY   ");
      break;
    case OP_RECOMMIT:
      printf("OP_RECOMMIT");
      break;
    case OP_SYNC:
      printf("OP_SYNC    ");
      break;
    case OP_LAST:
      printf("OP_LAST    ");
      break;
    case OP_TRUNCATE:
      printf("OP_TRUNCATE");
      break;
  }
  printf("%s", trail);
}

void
dkind_print(dkind_t dkind, const char *lead, const char *trail)
{
  printf("%s", lead);
  switch (dkind) {
    case DEC_NULL:
      printf("DEC_NULL");
      break;
    case DEC_CHAT:
      printf("DEC_CHAT");
      break;
    case DEC_JOIN:
      printf("DEC_JOIN");
      break;
    case DEC_PART:
      printf("DEC_PART");
      break;
  }
  printf("%s", trail);
}

void
paxos_header_print(struct paxos_header *hdr, const char *lead,
    const char *trail)
{
  printf("%s", lead);
  paxop_print(hdr->ph_opcode, "", " ");
  ppair_print(hdr->ph_ballot, "", " ");
  paxid_print(hdr->ph_inum, "", "");
  printf("%s", trail);
}

void
paxos_value_print(struct paxos_value *val, const char *lead,
    const char *trail)
{
  printf("%s", lead);
  dkind_print(val->pv_dkind, "", " ");
  ppair_print(val->pv_reqid, "", " ");
  paxid_print(val->pv_extra, "", "");
  printf("%s", trail);
}

void
paxos_acceptor_print(struct paxos_acceptor *acc, const char *lead,
    const char *trail)
{
  printf("%s", lead);
  paxid_print(acc->pa_paxid, "", ": ");
  printf("%*s", (int)acc->pa_size, (char *)acc->pa_desc);
  printf("%s", trail);
}

void
paxos_instance_print(struct paxos_instance *inst, const char *lead,
    const char *trail)
{
  paxos_header_print(&inst->pi_hdr, lead, "\n");
  paxos_value_print(&inst->pi_val, lead, "\n");
  printf("[%d/%d/%d] votes: %u / rejects: %u", inst->pi_committed,
      inst->pi_cached, inst->pi_learned, inst->pi_votes, inst->pi_rejects);
  printf("%s", trail);
}

void
paxos_request_print(struct paxos_request *req, const char *lead,
    const char *trail)
{
  paxos_value_print(&req->pr_val, lead, "\n");
  printf("%*s", (int)req->pr_size, (char *)req->pr_data);
  printf("%s", trail);
}
