#ifndef __TRILL_GNUTLS_H__
#define __TRILL_GNUTLS_H__

#include <gnutls/gnutls.h>
#include <gnutls/dtls.h>

struct trill_tls {
  gnutls_certificate_credentials_t tt_creds;
  gnutls_session_t tt_session;
};

#endif // __TRILL_GNUTLS_H__
