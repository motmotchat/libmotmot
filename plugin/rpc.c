/**
 * rpc.c - RPC calls to a motmot server
 */

#include <glib.h>
#include <msgpack.h>
#include <stdint.h>
#include <string.h>

#include "msgpackutil.h"
#include "prpl.h"
#include "rpc.h"
#include "sslconn.h"

// A return-nonzero assert instead of the typical abort() one
#define assert(x) \
  if (!(x)) { \
    g_warning("RPC Error: %s (%s:%d)", #x, __FILE__, __LINE__); \
    return 1; \
  }

////////////////////////////////////////////////////////////////////////////
//
// Protocol helper macros
//
// The work of checking and responding to protocol requests is as boring as it
// is verbose. Let's make our lives easier with some macros
//


// Most of what we're actually doing is typechecking
#define typecheck(obj, tipe) assert((obj)->type == MSGPACK_OBJECT_##tipe)

// msgpack's type names are unfortunately long
#define MSGPACK_OBJECT_INT MSGPACK_OBJECT_INTEGER
#define MSGPACK_OBJECT_UINT MSGPACK_OBJECT_POSITIVE_INTEGER
#define MSGPACK_OBJECT_STRING MSGPACK_OBJECT_RAW

// Strings require a bit more work to be useful. Remember to g_free the result
#define cstring(obj) g_strndup((obj)->via.raw.ptr, (obj)->via.raw.size)


// Doing this cast by ourselves is sort of tedious
#define pdata(buddy) ((struct pm_buddy *)(buddy)->proto_data)

// We commonly want to replace an old object (in a struct) with a new one,
// freeing whatever was there before if applicable
#define swapout(old, new)       \
  if (old != NULL) g_free((void *)old); \
  old = new


static int rpc_all_statuses(struct pm_account *, const msgpack_object *);
static int rpc_get_status_resp(struct pm_account *, const msgpack_object *);


// TODO: This file needs a lot of error handling. In particular,
// purple_ssl_write returns the number of bytes written, which might at some
// point involve some connection buffer that ensures yaks get written


int
rpc_dispatch(struct pm_account *account, const msgpack_object *obj)
{
  int64_t opcode;

  // Do a little sanity-checking of the received object
  // TODO: replace asserts with some sort of throw-to-the-user error handling
  assert(obj->type == MSGPACK_OBJECT_ARRAY);
  assert(obj->via.array.size >= 1);
  assert(obj->via.array.ptr[0].type == MSGPACK_OBJECT_NEGATIVE_INTEGER);

  opcode = obj->via.array.ptr[0].via.i64;

  switch (opcode) {
    // TODO: import from purplemot.c:867
    case OP_ALL_STATUS_RESPONSE:
      return rpc_all_statuses(account, obj->via.array.ptr + 1);
    case OP_GET_STATUS_RESP:
      return rpc_get_status_resp(account, obj->via.array.ptr + 1);
    /* TODO: finish importing all of this
    case OP_PUSH_FRIEND_ACCEPT:
    case OP_PUSH_CLIENT_STATUS:
      friend_name = deser_get_string(ar, 1, &error);
      if (error == PURPLEMOT_ERROR) {
        return;
      }
      status = deser_get_pos_int(ar, 2, &error);
      if (error == PURPLEMOT_ERROR) {
        g_free(friend_name);
        return;
      }
      update_remote_status(a, friend_name, status);

      if (opcode == OP_PUSH_FRIEND_ACCEPT) {
        break;
      }

      addr = deser_get_string(tuple, 2, &error);
      if (error == PURPLEMOT_ERROR) {
        return;
      }
      port = deser_get_pos_int(tuple, 3, &error);
      if (error == PURPLEMOT_ERROR) {
        return;
      }
      bud = purple_find_buddy(a, friend_name);
      if (bud == NULL) {
        return;
      }
      proto = bud->proto_data;
      proto->addr = addr;
      proto->port = port;
      g_free(friend_name);
      break;
    case OP_PUSH_FRIEND_REQUEST:
      purple_debug_info("purplemot", "getting friend request");
      friend_name = deser_get_string(ar, 1, &error);
      if (error == PURPLEMOT_ERROR) {
        return;
      }
      if (purple_find_buddy(a, friend_name) != NULL) {
        break;
      }
      conn->data = friend_name;

      purple_account_request_authorization(a, friend_name, NULL, NULL, NULL, FALSE, auth_cb, deny_cb, gc);
      break;
    case OP_ACCESS_DENIED:
    case OP_AUTH_FAILED:
      purple_connection_error_reason(gc, PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, _("Authentication failed, please check that your username and password are correct"));
      break;
    */
    default:
      return 1;
  }
}

////////////////////////////////////////////////////////////////////////////
//
// RPC response callbacks
//


static int
rpc_all_statuses(struct pm_account *account, const msgpack_object *obj)
{
  const msgpack_object *s, *e, *a;
  const char *name;
  PurpleBuddy *buddy;

  typecheck(obj, ARRAY);

  for (s = obj->via.array.ptr, e = s + obj->via.array.size; s != e; s++) {
    typecheck(s, ARRAY);
    assert(s->via.array.size == 3);
    a = s->via.array.ptr;

    // TODO(carl): This can be converted into a preprocessor map.
    // TODO(carl): Change the server to match this
    typecheck(a + 0, STRING);
    typecheck(a + 1, UINT);
    typecheck(a + 2, STRING);
    typecheck(a + 3, UINT);

    name = cstring(a + 0);

    if ((buddy = purple_find_buddy(account->pa, name)) == NULL) {
      buddy = purple_buddy_new(account->pa, name, NULL /* TODO: alias? */);
      buddy->proto_data = g_malloc0(sizeof(struct pm_buddy));
    }

    swapout(pdata(buddy)->ip, cstring(a + 2));
    pdata(buddy)->port = a[3].via.u64;
    //update_remote_status(account->pa, name, a[1].via.u64);

    g_free((void *)name);
  }

  return 0;
}

static int
rpc_get_status_resp(struct pm_account *account, const msgpack_object *obj)
{
  const msgpack_object *a;
  const char *name;
  PurpleBuddy *buddy = NULL;

  // TODO(carl): WTF. The data format here is different than the one given
  // above, which does the same thing (modulo a list). I don't get it.

  typecheck(obj, ARRAY);
  assert(obj->via.array.size == 4);

  a = obj->via.array.ptr;
  typecheck(a + 0, STRING);   // name
  typecheck(a + 1, UINT);     // status
  typecheck(a + 2, STRING);   // ip
  typecheck(a + 3, UINT);     // port

  name = cstring(a + 0);

  buddy = purple_find_buddy(account->pa, name);
  if (buddy == NULL) {
    return 1; // TODO(carl): what should we do here?
  }

  //update_remote_status(account->pa, name, a[2].via.u64);

  if (pdata(buddy)->ip != NULL) {
    g_free((void *)pdata(buddy)->ip);
  }
  swapout(pdata(buddy)->ip, cstring(a + 2));
  pdata(buddy)->port = a[2].via.u64;

  g_free((void *)name);

  return 0;
}

// TODO: the rest of them


////////////////////////////////////////////////////////////////////////////
//
// Client -> Server RPC calls
//

// Helper macros
#define rpc_op(y, op, nargs)                    \
  struct yak y;                                 \
  yak_init(&y, 1 + (nargs));                    \
  yak_pack(&y, int, op);

#define rpc_op_end(y, account)                  \
  purple_ssl_write(account->gsc, UNYAK(&yak));  \
  yak_destroy(&y);

void
rpc_login(struct pm_account *account)
{
  rpc_op(yak, OP_AUTHENTICATE_USER, 2);

  yak_cstring(&yak, purple_account_get_username(account->pa));
  yak_cstring(&yak, purple_account_get_password(account->pa));

  rpc_op_end(yak, account);
}

void
rpc_get_all_statuses(struct pm_account *account)
{
  rpc_op(yak, OP_GET_ALL_STATUSES, 0);
  rpc_op_end(yak, account);
}

void
rpc_register_friend(struct pm_account *account, const char *name)
{
  rpc_op(yak, OP_REGISTER_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, account);
}

void
rpc_unregister_friend(struct pm_account *account, const char *name)
{
  rpc_op(yak, OP_UNREGISTER_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, account);
}

void
rpc_get_status(struct pm_account *account, const char *name)
{
  rpc_op(yak, OP_GET_USER_STATUS, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, account);
}

void
rpc_register_status(struct pm_account *account, int status)
{
  rpc_op(yak, OP_REGISTER_STATUS, 1);
  yak_pack(&yak, int, status);
  rpc_op_end(yak, account);
}

void
rpc_accept_friend(struct pm_account *account, const char *name)
{
  rpc_op(yak, OP_ACCEPT_FRIEND, 1);
  yak_cstring(&yak, name);
  rpc_op_end(yak, account);
}
