#include <assert.h>
#include <stdio.h>
#include <glib.h>

GMainLoop *gmain;

int heartbeat(void *);

int
main(int argc, char *argv[])
{
  GMainContext *gcontext;

  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  // Let's do something silly to see if it actually works
  g_timeout_add_seconds(1, heartbeat, NULL);

  g_main_loop_run(gmain);
}

int
heartbeat(void *unused)
{
  printf("Poke.\n");
}
