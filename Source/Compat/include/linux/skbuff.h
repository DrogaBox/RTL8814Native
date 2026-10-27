/*
 * RTL8814Native — sk_buff shim for XNU
 *
 * DESIGN DECISION (important): skbs own STANDALONE IOMalloc buffers,
 * NOT mbufs. The vendor driver leans heavily on Linux buffer
 * arithmetic (headroom reserve/push/pull, tail growth, RX aggregation
 * buffers of up to MAX_RECVBUF_SZ with reused preallocated skbs).
 * Emulating that over mbuf leads and clusters is fragile; instead we
 * convert to/from mbufs ONLY at the two OS boundaries:
 *
 *   RX:  driver fills skb  -> rtl_netif_rx()  -> mbuf -> IONetwork
 *   TX:  IONetwork outputPacket(mbuf)         -> skb  -> ndo_start_xmit
 *
 * The copy cost is a single memcpy per packet — negligible against
 * USB 3.0 WiFi throughput, and it can be optimized later (phase 4)
 * with an mbuf-backed fast path if measurements justify it.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_SKBUFF_H_RTL8814
#define _LINUX_SKBUFF_H_RTL8814

#include <linux/types.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>    /* spinlock_t in struct sk_buff_head */
#include <linux/string.h>     /* memcpy in skb_copy               */

#ifdef __cplusplus
extern "C" {
#endif

#define SKB_MAX_ALLOC   (64 * 1024)          /* RX agg buffer ceiling */
#define SKB_HEADROOM    512                  /* TX reserve space      */

struct net_device;

struct sk_buff {
        /* Buffer layout (Linux semantics):
         *   head .... data .... tail .... end
         *   [headroom][payload  len][tailroom]                                */
        unsigned char *head, *data, *tail, *end;
        u32 len;
        u32 truesize;
        u32 data_len;

        struct net_device *dev;
        struct list_head list;                    /* queue linkage */
        atomic_t users;

        u8  cb[48];                               /* driver control block */
        u32 queue_mapping;
        u16 vlan_tci;
};

struct sk_buff_head {
        struct list_head list;
        u32 qlen;
        spinlock_t lock_dummy_unused;
};

/* ---- allocation ---- */
struct sk_buff *rtl_alloc_skb(unsigned int size);
static inline struct sk_buff *alloc_skb(unsigned int size, gfp_t flags)
{ (void)flags; return rtl_alloc_skb(size); }
static inline struct sk_buff *dev_alloc_skb(unsigned int len)
{ return rtl_alloc_skb(len + SKB_HEADROOM); }
static inline struct sk_buff *__dev_alloc_skb(unsigned int len, gfp_t flags)
{ (void)flags; return dev_alloc_skb(len); }
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
                                               unsigned int len)
{ struct sk_buff *s = rtl_alloc_skb(len + SKB_HEADROOM); if (s) s->dev = dev; return s; }

void rtl_kfree_skb(struct sk_buff *skb);
static inline void dev_kfree_skb(struct sk_buff *skb) { rtl_kfree_skb(skb); }
static inline void dev_kfree_skb_any(struct sk_buff *skb) { rtl_kfree_skb(skb); }
static inline void kfree_skb(struct sk_buff *skb) { rtl_kfree_skb(skb); }
static inline void consume_skb(struct sk_buff *skb) { rtl_kfree_skb(skb); }

/* ---- data pointer arithmetic ---- */
static inline unsigned char *__skb_put(struct sk_buff *skb, unsigned int len)
{
        unsigned char *tmp = skb->tail;
        skb->tail += len; skb->len += len;
        return tmp;
}
#define skb_put(skb, len) __skb_put(skb, len)

static inline unsigned char *__skb_push(struct sk_buff *skb, unsigned int len)
{
        skb->data -= len; skb->len += len;
        return skb->data;
}
#define skb_push(skb, len) __skb_push(skb, len)

static inline unsigned char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
        skb->len -= len;
        return skb->data += len;
}
#define skb_pull(skb, len) __skb_pull(skb, len)

static inline void skb_reserve(struct sk_buff *skb, unsigned int len)
{ skb->data += len; skb->tail += len; }

static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{ if (skb->len > len) { skb->len = len; skb->tail = skb->data + len; } }

static inline unsigned int skb_headroom(const struct sk_buff *skb)
{ return (unsigned int)(skb->data - skb->head); }
static inline unsigned int skb_tailroom(const struct sk_buff *skb)
{ return (unsigned int)(skb->end - skb->tail); }
static inline unsigned int skb_is_nonlinear(const struct sk_buff *skb)
{ return skb->data_len != 0; }

static inline struct sk_buff *skb_get(struct sk_buff *skb)
{ atomic_inc(&skb->users); return skb; }

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{ skb->tail = skb->data; }
static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{ skb->tail = skb->data + offset; }
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{ return skb->tail; }
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{ return skb->end; }
static inline void skb_reset_mac_header(struct sk_buff *skb) { (void)skb; }
static inline void *skb_mac_header(const struct sk_buff *skb) { (void)skb; return NULL; }

static inline struct sk_buff *skb_copy(const struct sk_buff *src, gfp_t gfp)
{
        struct sk_buff *n = rtl_alloc_skb(src->len + SKB_HEADROOM);
        if (n) { skb_reserve(n, (unsigned int)(src->data - src->head));
                 memcpy(__skb_put(n, src->len), src->data, src->len); }
        (void)gfp;
        return n;
}
static inline struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t gfp)
{ (void)gfp; return skb_copy(skb, 0); }
static inline int skb_cloned(const struct sk_buff *skb) { (void)skb; return 0; }
static inline int pskb_expand_head(struct sk_buff *skb, int a, int b, gfp_t g)
{ (void)skb; (void)a; (void)b; (void)g; return 0; }
static inline int skb_realloc_headroom(struct sk_buff *skb, unsigned int h)
{ (void)skb; (void)h; return 0; }

/* ---- queues ---- */
static inline void skb_queue_head_init(struct sk_buff_head *q)
{ INIT_LIST_HEAD(&q->list); q->qlen = 0; rtl_spin_init(&q->lock_dummy_unused); }
static inline void __skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{ list_add_tail(&skb->list, &q->list); q->qlen++; }
static inline void skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{ spin_lock(&q->lock_dummy_unused); __skb_queue_tail(q, skb); spin_unlock(&q->lock_dummy_unused); }
static inline void __skb_queue_head(struct sk_buff_head *q, struct sk_buff *skb)
{ list_add(&skb->list, &q->list); q->qlen++; }
static inline struct sk_buff *skb_peek(const struct sk_buff_head *q)
{ return list_empty(&q->list) ? NULL :
         list_first_entry(&q->list, struct sk_buff, list); }
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *q)
{
        struct sk_buff *s = skb_peek(q);
        if (s) { list_del(&s->list); q->qlen--; }
        return s;
}
static inline struct sk_buff *skb_dequeue(struct sk_buff_head *q)
{
        spin_lock(&q->lock_dummy_unused);
        struct sk_buff *s = __skb_dequeue(q);
        spin_unlock(&q->lock_dummy_unused);
        return s;
}
static inline int skb_queue_empty(const struct sk_buff_head *q)
{ return list_empty(&q->list); }
static inline __u32 skb_queue_len(const struct sk_buff_head *q)
{ return q->qlen; }
static inline void skb_queue_purge(struct sk_buff_head *q)
{
        struct sk_buff *s;
        while ((s = skb_dequeue(q)) != NULL)
                rtl_kfree_skb(s);
}
#define skb_queue_walk(queue, skb) \
        list_for_each_entry(skb, &(queue)->list, list)

/* dst/src used by core xmit paths */
static inline int skb_cow(struct sk_buff *skb, unsigned int headroom)
{ (void)skb; (void)headroom; return 0; }

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_SKBUFF_H_RTL8814 */
