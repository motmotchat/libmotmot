#include <stdio.h>
#include <glib.h>

#include "network.h"

int
main(int argc, char *argv[])
{
  struct trill_connection *tc;

  tc = trill_connection_new();

  printf("%d\n", tc->tc_port);
  return 0;
}
