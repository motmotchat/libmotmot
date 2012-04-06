#include <assert.h>
#include <glib.h>

GMainLoop *gmain;

int
main(int argc, char *argv[])
{
  GMainContext *gcontext;

  gcontext = g_main_context_new();
  assert(gcontext != NULL);

  gmain = g_main_loop_new(gcontext, FALSE);

  g_main_loop_run(gmain);
}
