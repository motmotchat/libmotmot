/**
 * email.c - Plume handle utilities.
 */

#include <string.h>

/**
 * email_chars - Email address permissibility table for ASCII characters.
 */
static const char email_chars[128] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,

  0, 1, 0, 1, 1, 1, 1, 1, // 33, 35-39
  0, 0, 1, 1, 0, 1, 0, 1, // 42, 43, 45, 47
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 1, 0, 1, // 61, 63

  0, 1, 1, 1, 1, 1, 1, 1, // A - Z...
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 0, 0, 0, 1, 1, // 94-95

  1, 1, 1, 1, 1, 1, 1, 1, // 96, a - z...
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 0, // 123-126
};

/**
 * email_get_domain - Validate an email address and return a pointer to the
 * domain part.
 *
 * Plume handles are a subset of those addresses allowed by RFC 5322.  In
 * particular, the following restrictions are added:
 * - Special characters, including / "(),:;<>@[\]/, are disallowed completely.
 * - Characters are restricted to ASCII; unicode is not permitted.
 * - Comments are not permitted.
 */
char *
email_get_domain(const char *email)
{
  int i, n, was_dot;
  char *at_sign;

  // Email addresses may be at most 254 characters.
  if (strlen(email) > 254) {
    return NULL;
  }

  // Make sure the address has a '@'.
  at_sign = strchr(email, '@');
  if (at_sign == NULL) {
    return NULL;
  }

  // Get the length of the local part and the domain pointer.
  n = (int)(at_sign - email);

  // Last character cannot be '.'.
  if (email[n - 1] == '.') {
    return NULL;
  }

  was_dot = 1;

  for (i = 0; i < n; ++i) {
    // No character following '.', nor the first character, may be '.'.
    if (was_dot && email[i] == '.') {
      return NULL;
    }
    was_dot = email[i] == '.';

    // Ensure that each character is a permitted ASCII value.
    if (email[i] < 0 || !email_chars[(int)email[i]]) {
      return NULL;
    }
  }

  return at_sign + 1;
}
