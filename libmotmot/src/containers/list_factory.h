/**
 * list_factory.h - Macros for implementing and declaring BSD list types for
 * an arbitrary paxos_* struct.
 *
 * All list factory macros assume that the element struct...
 * - includes a BSD list entry field;
 * - has a name of the form paxos_*; and
 * - includes an ID field which uniquely identifies and totally orders all
 *   instances of the struct.
 */

#include "containers/list.h"

/**
 * Factory for implementing list initialization for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define LIST_IMPLEMENT_INIT(name)                                           \
  inline void                                                               \
  name##_container_init(name##_container *head)                             \
  {                                                                         \
    LIST_INIT(head);                                                        \
  }

/**
 * Factory for implementing list creation for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 */
#define LIST_IMPLEMENT_NEW(name)                                            \
  inline name##_container *                                                 \
  name##_container_new()                                                    \
  {                                                                         \
    name##_container *head = g_malloc(sizeof(*head));                       \
    name##_container_init(head);                                            \
    return head;                                                            \
  }

/**
 * Factory for implementing list destruction for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 * @param le_field    Name of the struct's list entry field.
 * @param destroy     Callback for destroying an element struct; should have
 *                    signature:  void (*)(struct paxos_{name} *);
 */
#define LIST_IMPLEMENT_DESTROY(name, le_field, destroy)                     \
  void                                                                      \
  name##_container_destroy(name##_container *head)                          \
  {                                                                         \
    struct paxos_##name *it;                                                \
                                                                            \
    LIST_WHILE_FIRST(it, head) {                                            \
      LIST_REMOVE(head, it, le_field);                                      \
      destroy(it);                                                          \
    }                                                                       \
  }

#define LIST_ITER_CMP_FWD(x, y)   (x) < (y)
#define LIST_ITER_CMP_REV(x, y)   (x) > (y)

#define LIST_FOREACH_FWD         LIST_FOREACH
#define LIST_INSERT_END_FWD      LIST_INSERT_TAIL
#define LIST_INSERT_END_REV      LIST_INSERT_HEAD
#define LIST_INSERT_REL_FWD      LIST_INSERT_BEFORE
#define LIST_INSERT_REL_REV      LIST_INSERT_AFTER

/**
 * Factory for implementing list find for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 * @param id_t        Type of the ID used to order and to uniquely identify
 *                    the paxos_{name} elements.
 * @param le_field    Name of the struct's list entry field.
 * @param id_field    Name of the struct's ID field.
 * @param compare     Callback for comparing two IDs; should have signature:
 *                    int (*)(id_t, id_t);
 * @param _dir        Flag, either `_FWD' or `_REV', that determines whether
 *                    we search the list forwards or backwards.
 */
#define LIST_IMPLEMENT_FIND(name, id_t, le_field, id_field, compare, _dir)  \
  struct paxos_##name *                                                     \
  name##_find(name##_container *head, id_t id)                              \
  {                                                                         \
    int cmp;                                                                \
    struct paxos_##name *it;                                                \
                                                                            \
    if (LIST_EMPTY(head)) {                                                 \
      return NULL;                                                          \
    }                                                                       \
                                                                            \
    LIST_FOREACH##_dir(it, head, le_field) {                                \
      cmp = compare(id, it->id_field);                                      \
      if (cmp == 0) {                                                       \
        return it;                                                          \
      } else if (LIST_ITER_CMP##_dir(cmp, 0)) {                             \
        break;                                                              \
      }                                                                     \
    }                                                                       \
                                                                            \
    return NULL;                                                            \
  }

/**
 * Factory for implementing list insert for a Paxos struct.
 *
 * @param name        Name of the struct, i.e., struct paxos_{name}.
 * @param le_field    Name of the struct's list entry field.
 * @param id_field    Name of the struct's ID field.
 * @param compare     Callback for comparing two IDs; should have signature:
 *                    int (*)(id_t, id_t);
 * @param _dir        Flag, either `_FWD' or `_REV', that determines whether
 *                    we search the list forwards or backwards.
 */
#define LIST_IMPLEMENT_INSERT(name, le_field, id_field, compare, _dir)      \
  struct paxos_##name *                                                     \
  name##_insert(name##_container *head, struct paxos_##name *elt)           \
  {                                                                         \
    int cmp;                                                                \
    struct paxos_##name *it;                                                \
                                                                            \
    LIST_FOREACH##_dir(it, head, le_field) {                                \
      cmp = compare(elt->id_field, it->id_field);                           \
      if (cmp == 0) {                                                       \
        return it;                                                          \
      } else if (LIST_ITER_CMP##_dir(cmp, 0)) {                             \
        LIST_INSERT_REL##_dir(head, it, elt, le_field);                     \
        return elt;                                                         \
      }                                                                     \
    }                                                                       \
                                                                            \
    LIST_INSERT_END##_dir(head, elt, le_field);                             \
    return elt;                                                             \
  }

/**
 * Declare a list type and list utility prototypes.
 */
#define LIST_DECLARE(name, id_t)                                            \
  typedef LIST_HEAD(name##_container, paxos_##name) name##_container;       \
  inline void name##_container_init(name##_container *);                    \
  inline name##_container *name##_container_new(void);                      \
  void name##_container_destroy(name##_container *);                        \
  struct paxos_##name *name##_find(name##_container *, id_t);               \
  struct paxos_##name *name##_insert(name##_container *,                    \
      struct paxos_##name *);

/**
 * Implement a list container for a particular Paxos struct.
 */
#define LIST_IMPLEMENT(name, id_t, le_field, id_field, cmp, dstr, _f, _i)   \
  LIST_IMPLEMENT_INIT(name);                                                \
  LIST_IMPLEMENT_NEW(name);                                                 \
  LIST_IMPLEMENT_DESTROY(name, le_field, dstr);                             \
  LIST_IMPLEMENT_FIND(name, id_t, le_field, id_field, cmp, _f);             \
  LIST_IMPLEMENT_INSERT(name, le_field, id_field, cmp, _i);
