/**
 * list.h - Generic list macros and definitions based on BSD's circular
 * queues.  Embeds inside structs to enable their use as list nodes.
 *
 * Most of the functionality is taken directly from the FreeBSD kernel,
 * here: http://fxr.watson.org/fxr/source/bsd/sys/queue.h?v=xnu-792
 */

#ifndef __LIST_H__
#define __LIST_H__

#define LIST_HEAD(name, type)                   \
  struct name {                                 \
    struct type *lh_first;                      \
    struct type *lh_last;                       \
    unsigned lh_count;                          \
  }

#define LIST_ENTRY(type)                        \
  struct {                                      \
    struct type *le_next;                       \
    struct type *le_prev;                       \
  }

#define LIST_EMPTY(head)  ((head)->lh_first == (void *)(head))

#define LIST_FIRST(head)  ((head)->lh_first)

#define LIST_LAST(head)   ((head)->lh_last)

#define LIST_NEXT(elm, field)  ((elm)->field.le_next)

#define LIST_PREV(elm, field)  ((elm)->field.le_prev)

/*
 * Iteration; "var" should be declared of type "type *".
 */
#define LIST_FOREACH(var, head, field)          \
    for ((var) = (head)->lh_first;              \
         (var) != (void *)(head);               \
         (var) = (var)->field.le_next)

#define LIST_FOREACH_REV(var, head, field)      \
    for ((var) = (head)->lh_last;               \
         (var) != (void *)(head);               \
         (var) = (var)->field.le_prev)

#define LIST_INIT(head) do {                    \
        (head)->lh_first = (void *)(head);      \
        (head)->lh_last = (void *)(head);       \
        (head)->lh_count = 0;                   \
    } while (0)

#define LIST_CLEANUP(head) do {                                 \
        assert((head) != NULL);                                 \
        assert((head)->lh_first == (void *)(head));             \
        assert((head)->lh_last == (void *)(head));              \
        assert(LIST_EMPTY((head)));                             \
        assert((head)->lh_count == 0);                          \
    } while (0)

/*
 * Crawling insertions; listelm is in the list, elm is to be inserted.
 */
#define LIST_INSERT_AFTER(head, listelm, elm, field) do {             \
        (elm)->field.le_next = (listelm)->field.le_next;              \
        (elm)->field.le_prev = (listelm);                             \
        if ((listelm)->field.le_next == (void *)(head))               \
            (head)->lh_last = (elm);                                  \
        else                                                          \
            (listelm)->field.le_next->field.le_prev = (elm);          \
        (listelm)->field.le_next = (elm);                             \
        (head)->lh_count++;                                           \
    } while (0)

#define LIST_INSERT_BEFORE(head, listelm, elm, field) do {            \
        (elm)->field.le_next = (listelm);                             \
        (elm)->field.le_prev = (listelm)->field.le_prev;              \
        if ((listelm)->field.le_prev == (void *)(head))               \
            (head)->lh_first = (elm);                                 \
        else                                                          \
            (listelm)->field.le_prev->field.le_next = (elm);          \
        (listelm)->field.le_prev = (elm);                             \
        (head)->lh_count++;                                           \
    } while (0)

#define LIST_INSERT_HEAD(head, elm, field) do {                       \
        (elm)->field.le_next = (head)->lh_first;                      \
        (elm)->field.le_prev = (void *)(head);                        \
        if ((head)->lh_last == (void *)(head))                        \
            (head)->lh_last = (elm);                                  \
        else                                                          \
            (head)->lh_first->field.le_prev = (elm);                  \
        (head)->lh_first = (elm);                                     \
        (head)->lh_count++;                                           \
    } while (0)

#define LIST_INSERT_TAIL(head, elm, field) do {                       \
        (elm)->field.le_next = (void *)(head);                        \
        (elm)->field.le_prev = (head)->lh_last;                       \
        if ((head)->lh_first == (void *)(head))                       \
            (head)->lh_first = (elm);                                 \
        else                                                          \
            (head)->lh_last->field.le_next = (elm);                   \
        (head)->lh_last = (elm);                                      \
        (head)->lh_count++;                                           \
    } while (0)

#define LIST_REMOVE(head, elm, field) do {                            \
        if ((elm)->field.le_next == (void *)(head))                   \
            (head)->lh_last = (elm)->field.le_prev;                   \
        else                                                          \
            (elm)->field.le_next->field.le_prev =                     \
                (elm)->field.le_prev;                                 \
        if ((elm)->field.le_prev == (void *)(head))                   \
            (head)->lh_first = (elm)->field.le_next;                  \
        else                                                          \
            (elm)->field.le_prev->field.le_next =                     \
                (elm)->field.le_next;                                 \
        (head)->lh_count--;                                           \
    } while (0)

#endif /* _LIST_H_ */
