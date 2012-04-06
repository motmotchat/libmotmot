#include <assert.h>
#include <glib.h>

GMainLoop *gmain;

int
main(int argc, char *argv[])
{
  GMainContext *gcontext;

  gmain = g_main_loop_new(g_main_context_default(), FALSE);

  // Do stuff here

  g_main_loop_run(gmain);
}
