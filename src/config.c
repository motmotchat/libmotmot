/**
 * config.c - Motmot configuration utilities.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "common/log.h"

/**
 * motmot_home_dir - Returns a constant string giving the path of the Motmot
 * home directory.
 */
const char *
motmot_home_dir()
{
  static char *motmot_path = NULL;

  char *env_home;

  if (motmot_path == NULL) {
    motmot_path = malloc(PATH_MAX);
    memset(motmot_path, 0, PATH_MAX);

    env_home = getenv("HOME");
    log_assert(env_home, "No $HOME set");

    strncpy(motmot_path, env_home, PATH_MAX);
    strncat(motmot_path, "/.motmot", 8);
  }

  return motmot_path;
}
