/**
 * x509.c - Plume client certificate and encryption routines.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "common/log.h"

#define CERT_SIZE 4096

const char *
self_key_path()
{
  static char *key_path = NULL;

  if (key_path == NULL) {
    key_path = malloc(PATH_MAX);
    memcpy(key_path, motmot_home_dir(), PATH_MAX);

    strncat(key_path, "/self/mxw@mxawng.com.key", 24);
  }

  return key_path;
}

const char *
self_cert_path()
{
  static char *cert_path = NULL;

  if (cert_path == NULL) {
    cert_path = malloc(PATH_MAX);
    memcpy(cert_path, motmot_home_dir(), PATH_MAX);

    strncat(cert_path, "/self/mxw@mxawng.com.crt", 24);
  }

  return cert_path;
}

const char *
self_cert()
{
  static char *cert = NULL;

  FILE *cert_file;

  if (cert == NULL) {
    cert = malloc(CERT_SIZE + 1);
    memset(cert, 0, CERT_SIZE + 1);

    cert_file = fopen(self_cert_path(), "r");
    log_assert(cert_file, "self_cert: No cert found for %s", "/mxw@mxawng.com");
    fread(cert, sizeof(char), CERT_SIZE, cert_file);
    fclose(cert_file);
  }

  return cert;
}
