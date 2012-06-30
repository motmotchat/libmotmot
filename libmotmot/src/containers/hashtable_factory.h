/**
 * hashtable_factory.h - Macros for implementing and declaring GLib hash
 * table types for an arbitrary paxos_* struct.
 *
 * The factory for container insertion assumes that the value struct for the
 * hashtable contains the key as a field, either inlined or via pointer.
 */

#include <assert.h>
#include <glib.h>

/**
 * Initialization without allocation is not supported for hash tables.
 */
#define HASHTABLE_IMPLEMENT_INIT(name)                                      \
  inline void                                                               \
  name##_container_init(name##_container *ht)                               \
  {                                                                         \
    assert(0);                                                              \
  }

/**
 * Factory for implementing hashtable creation for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 * @param hash        Hash function for table keys; should have signature:
 *                    unsigned (*)(const void *);
 * @param equals      Compare function for table keys; should return nonzero
 *                    iff equals and have signature:
 *                    int (*)(const void *, const void *);
 */
#define HASHTABLE_IMPLEMENT_NEW(name, hash, equals)                         \
  inline name##_container *                                                 \
  name##_container_new()                                                    \
  {                                                                         \
    return (name##_container *)g_hash_table_new(hash, equals);              \
  }

/**
 * Factory for implementing hashtable destruction for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define HASHTABLE_IMPLEMENT_DESTROY(name)                                   \
  inline void                                                               \
  name##_container_destroy(name##_container *ht)                            \
  {                                                                         \
    g_hash_table_destroy(ht);                                               \
  }

/**
 * Factory for implementing hashtable lookup for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define HASHTABLE_IMPLEMENT_FIND(name)                                      \
  inline struct paxos_##name *                                              \
  name##_find(name##_container *ht, void *key)                              \
  {                                                                         \
    return g_hash_table_lookup(ht, key);                                    \
  }

/**
 * Factory for implementing hashtable insertion for a Paxos struct that
 * contains its key as an inlined field.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define HASHTABLE_IMPLEMENT_INSERT_INL(name, key_field)                     \
  inline struct paxos_##name *                                              \
  name##_insert(name##_container *ht, struct paxos_##name *value)           \
  {                                                                         \
    g_hash_table_insert(ht, &value->key_field, value);                      \
    return value;                                                           \
  }

/**
 * Factory for implementing hashtable insertion for a Paxos struct that
 * contains a pointer to its key as a field.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define HASHTABLE_IMPLEMENT_INSERT_PTR(name, key_field)                     \
  inline struct paxos_##name *                                              \
  name##_insert(name##_container *ht, struct paxos_##name *value)           \
  {                                                                         \
    g_hash_table_insert(ht, value->key_field, value);                       \
    return value;                                                           \
  }

/**
 * Declare a hashtable type and hashtable utility prototypes.
 */
#define HASHTABLE_DECLARE(name)                                             \
  typedef GHashTable name##_container;                                      \
  inline void name##_container_init(name##_container *);                    \
  inline name##_container *name##_container_new(void);                      \
  inline void name##_container_destroy(name##_container *);                 \
  inline struct paxos_##name *name##_find(name##_container *, void *);      \
  inline struct paxos_##name *name##_insert(name##_container *,             \
      struct paxos_##name *);

/**
 * Implement a list container for a particular Paxos struct.
 */
#define HASHTABLE_IMPLEMENT(name, key_field, hash, equals, _fkind)          \
  HASHTABLE_IMPLEMENT_INIT(name);                                           \
  HASHTABLE_IMPLEMENT_NEW(name, hash, equals);                              \
  HASHTABLE_IMPLEMENT_DESTROY(name);                                        \
  HASHTABLE_IMPLEMENT_FIND(name);                                           \
  HASHTABLE_IMPLEMENT_INSERT##_fkind(name, key_field);
