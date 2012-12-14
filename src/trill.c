#include "crypto.h"
#include "network.h"
#include "trill.h"

trill_want_write_callback_t trill_want_write_callback;
trill_want_timeout_callback_t trill_want_timeout_callback;

int
trill_init(const struct trill_callback_vtable *vtable)
{
  trill_want_write_callback = vtable->want_write_callback;
  trill_want_timeout_callback = vtable->want_timeout_callback;

  return trill_crypto_init();
}
