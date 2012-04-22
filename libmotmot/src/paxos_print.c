/**
 * paxos_print.c - Paxos object printers.
 */
#include "paxos.h"
#include "paxos_print.h"

#include <stdio.h>

void
paxop_print(paxop_t op)
{
  switch (op) {
    case OP_PREPARE:
      printf("OP_PREPARE");
      break;
    case OP_PROMISE:
      printf("OP_PROMISE");
      break;
    case OP_DECREE:
      printf("OP_DECREE");
      break;
    case OP_ACCEPT:
      printf("OP_ACCEPT");
      break;
    case OP_COMMIT:
      printf("OP_COMMIT");
      break;
    case OP_WELCOME:
      printf("OP_WELCOME");
      break;
    case OP_GREET:
      printf("OP_GREET");
      break;
    case OP_HELLO:
      printf("OP_HELLO");
      break;
    case OP_PTMY:
      printf("OP_PTMY");
      break;
    case OP_REQUEST:
      printf("OP_REQUEST");
      break;
    case OP_RETRIEVE:
      printf("OP_RETRIEVE");
      break;
    case OP_RESEND:
      printf("OP_RESEND");
      break;
    case OP_REDIRECT:
      printf("OP_REDIRECT");
      break;
    case OP_SYNC:
      printf("OP_SYNC");
      break;
    case OP_TRUNCATE:
      printf("OP_TRUNCATE");
      break;
  }
}
