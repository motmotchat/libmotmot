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
      printf("OP_PREPARE\n");
      break;
    case OP_PROMISE:
      printf("OP_PROMISE\n");
      break;
    case OP_DECREE:
      printf("OP_DECREE\n");
      break;
    case OP_ACCEPT:
      printf("OP_ACCEPT\n");
      break;
    case OP_COMMIT:
      printf("OP_COMMIT\n");
      break;
    case OP_WELCOME:
      printf("OP_WELCOME\n");
      break;
    case OP_GREET:
      printf("OP_GREET\n");
      break;
    case OP_HELLO:
      printf("OP_HELLO\n");
      break;
    case OP_PTMY:
      printf("OP_PTMY\n");
      break;
    case OP_REQUEST:
      printf("OP_REQUEST\n");
      break;
    case OP_RETRIEVE:
      printf("OP_RETRIEVE\n");
      break;
    case OP_RESEND:
      printf("OP_RESEND\n");
      break;
    case OP_REDIRECT:
      printf("OP_REDIRECT\n");
      break;
    case OP_SYNC:
      printf("OP_SYNC\n");
      break;
    case OP_TRUNCATE:
      printf("OP_TRUNCATE\n");
      break;
  }
}
