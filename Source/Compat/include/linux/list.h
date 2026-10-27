/*
 * RTL8814Native — intrusive doubly-linked circular list (Linux list.h)
 *
 * Adapted from the Linux kernel list implementation (GPL-2.0) — same
 * license as the driver being ported, so the canonical semantics are
 * preserved exactly. The vendor driver depends on subtle list_head
 * behavior (poisoning, list_for_each_entry_safe, etc.).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0 (list core: Linux kernel)
 */
#ifndef _LINUX_LIST_H_RTL8814
#define _LINUX_LIST_H_RTL8814

#include <linux/types.h>
#include <linux/prefetch.h>

#ifdef __cplusplus
extern "C" {
#endif

struct list_head {
        struct list_head *next, *prev;
};

struct hlist_head {
        struct hlist_node *first;
};

struct hlist_node {
        struct hlist_node *next, **pprev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
        struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
        list->next = list;
        list->prev = list;
}

static inline void __list_add(struct list_head *newl,
                              struct list_head *prev,
                              struct list_head *next)
{
        next->prev = newl;
        newl->next = next;
        newl->prev = prev;
        prev->next = newl;
}

static inline void list_add(struct list_head *newl, struct list_head *head)
{
        __list_add(newl, head, head->next);
}

static inline void list_add_tail(struct list_head *newl, struct list_head *head)
{
        __list_add(newl, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
        next->prev = prev;
        prev->next = next;
}

static inline void __list_del_entry(struct list_head *entry)
{
        __list_del(entry->prev, entry->next);
}

static inline void list_del(struct list_head *entry)
{
        __list_del(entry->prev, entry->next);
        entry->next = (struct list_head *)0xDEADBEEF;
        entry->prev = (struct list_head *)0xDEADBEEF;
}

static inline void list_del_init(struct list_head *entry)
{
        __list_del_entry(entry);
        INIT_LIST_HEAD(entry);
}

static inline void list_replace(struct list_head *old, struct list_head *newl)
{
        newl->next = old->next;
        newl->next->prev = newl;
        newl->prev = old->prev;
        newl->prev->next = newl;
}

static inline int list_empty(const struct list_head *head)
{
        return head->next == head;
}

static inline int list_empty_careful(const struct list_head *head)
{
        struct list_head *next = head->next;
        return (next == head) && (next == head->prev);
}

static inline void list_splice_init(struct list_head *list,
                                    struct list_head *head)
{
        if (!list_empty(list)) {
                struct list_head *first = list->next;
                struct list_head *last = list->prev;
                struct list_head *at = head->next;
                first->prev = head;
                head->next = first;
                last->next = at;
                at->prev = last;
                INIT_LIST_HEAD(list);
        }
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
        __list_del_entry(list);
        list_add(list, head);
}

static inline void list_move_tail(struct list_head *list, struct list_head *head)
{
        __list_del_entry(list);
        list_add_tail(list, head);
}

#define list_entry(ptr, type, member) \
        container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
        list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
        list_entry((pos)->member.next, __typeof__(*(pos)), member)

#define list_for_each(pos, head) \
        for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_prev(pos, head) \
        for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each_safe(pos, n, head) \
        for (pos = (head)->next, n = pos->next; pos != (head); \
             pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member)                          \
        for (pos = list_first_entry(head, __typeof__(*pos), member);        \
             &pos->member != (head);                                    \
             pos = list_next_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member)                  \
        for (pos = list_first_entry(head, __typeof__(*pos), member),        \
             n = list_next_entry(pos, member);                          \
             &pos->member != (head);                                    \
             pos = n, n = list_next_entry(n, member))

#define list_for_each_entry_reverse(pos, head, member)                  \
        for (pos = list_last_entry(head, __typeof__(*pos), member);         \
             &pos->member != (head);                                    \
             pos = list_entry(pos->member.prev, __typeof__(*pos), member))

/* hlist — used by rhashtable paths in os_dep.
 * Positional initializers (not designated): C++17 tolerates the
 * positional form in strict mode without extension warnings. */
#define HLIST_HEAD_INIT { NULL }
#define HLIST_HEAD(name) struct hlist_head name = { NULL }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_NODE(struct hlist_node *h)
{
        h->next = NULL;
        h->pprev = NULL;
}

static inline int hlist_unhashed(const struct hlist_node *h)
{
        return !h->pprev;
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
        struct hlist_node *first = h->first;
        n->next = first;
        if (first)
                first->pprev = &n->next;
        h->first = n;
        n->pprev = &h->first;
}

static inline void __hlist_del(struct hlist_node *n)
{
        struct hlist_node *next = n->next;
        struct hlist_node **pprev = n->pprev;
        *pprev = next;
        if (next)
                next->pprev = pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
        __hlist_del(n);
        n->next = (struct hlist_node *)0xDEADBEEF;
        n->pprev = (struct hlist_node **)0xDEADBEEF;
}

static inline void hlist_del_init(struct hlist_node *n)
{
        if (!hlist_unhashed(n)) {
                __hlist_del(n);
                INIT_HLIST_NODE(n);
        }
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define hlist_for_each(pos, head) \
        for (pos = (head)->first; pos; pos = pos->next)

#define hlist_for_each_safe(pos, n, head) \
        for (pos = (head)->first; pos && ({ n = pos->next; 1; }); \
             pos = n)

#define hlist_for_each_entry(tpos, pos, head, member)                    \
        for (pos = (head)->first;                                        \
             pos && ({ tpos = hlist_entry(pos, __typeof__(*tpos), member); 1; }); \
             pos = pos->next)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_LIST_H_RTL8814 */
