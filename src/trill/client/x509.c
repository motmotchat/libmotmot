/**
 * x509.c - Plume client certificate and encryption routines.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define CERT_SIZE 4096

char *
self_cert()
{
  static char *cert = NULL;

  char *cert_path;

  if (cert == NULL) {
    cert = malloc(CERT_SIZE + 1);
    memset(cert, 0, CERT_SIZE + 1);

    cert_path = malloc(PATH_MAX);
    memcpy(cert_path, motmot_home_dir(), PATH_MAX);

    strncat(cert_path, "/self/mxw@mxawng.com.crt", 24);

    free(cert_path);
  }

  return NULL;
}
