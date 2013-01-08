/**
 * error.c - Plume error utilities.
 */

#include <ares.h>

#include "plume/plume.h"

static const char *errtext[] = {
  "Successful completion",
  "Connection was already attempted",
  "Out of memory",
  "Error reading file",
  "Invalid identity cert",
  "DNS lookup error",
  "TLS handshake error",
  "Unknown fatal error",
};

const char *
plume_strerror(enum plume_status code)
{
  return errtext[code];
}

enum plume_status
error_ares(int code)
{
  switch (code) {
    case ARES_SUCCESS:
      return PLUME_SUCCESS;

    case ARES_ENODATA:
    case ARES_EFORMERR:
    case ARES_ESERVFAIL:
    case ARES_ENOTFOUND:
    case ARES_ENOTIMP:
    case ARES_EREFUSED:
    case ARES_EBADFAMILY:
    case ARES_EBADRESP:
    case ARES_ECONNREFUSED:
    case ARES_ETIMEOUT:
      return PLUME_EDNS;

    case ARES_EBADQUERY:
    case ARES_EBADNAME:
    case ARES_ENONAME:
      return PLUME_EIDENTITY;

    case ARES_EOF:
    case ARES_EFILE:
      return PLUME_EFILE;

    case ARES_ENOMEM:
      return PLUME_ENOMEM;

    case ARES_ENOTINITIALIZED:
    case ARES_EDESTRUCTION:
    case ARES_EBADSTR:
    case ARES_EBADFLAGS:
    case ARES_EBADHINTS:
    case ARES_ELOADIPHLPAPI:
    case ARES_EADDRGETNETWORKPARAMS:
    case ARES_ECANCELLED:
    default:
      return PLUME_EFATAL;
  }
}
