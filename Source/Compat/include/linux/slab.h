/*
 * RTL8814Native — allocator shim for XNU (kmalloc/kfree)
 *
 * All allocations route through IOMalloc/IOFree. kzalloc zeroes.
 * GFP flags collapse to a single behavior (kernel allocations are
 * non-sleeping in atomic paths — IOMalloc never blocks in a harmful
 * way, but avoid kzalloc from true interrupt context; the vendor
 * driver preallocates its recv buffers for exactly this reason).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_SLAB_H_RTL8814
#define _LINUX_SLAB_H_RTL8814

#include <linux/types.h>
#include <linux/string.h>     /* memset/memcpy for the kzalloc inlines */

#ifdef __cplusplus
extern "C" {
#endif

/* forward decl — vmalloc_to_page is a no-op on the USB path but the
 * type must be complete enough to cast to */
struct page;

#define GFP_KERNEL   0
#define GFP_ATOMIC   1
#define GFP_DMA      0

void *rtl_malloc(size_t size);
void  rtl_free(void *p);

static inline void *kmalloc(size_t size, gfp_t flags)
{
        (void)flags;
        return rtl_malloc(size);
}

static inline void *kzalloc(size_t size, gfp_t flags)
{
        void *p = kmalloc(size, flags);
        if (p)
                memset(p, 0, size);
        return p;
}

static inline void *kmemdup(const void *src, size_t len, gfp_t flags)
{
        void *p = kmalloc(len, flags);
        if (p)
                memcpy(p, src, len);
        return p;
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
        return kzalloc(n * size, flags);
}

static inline void kfree(const void *p)
{
        rtl_free((void *)p);
}

/* vmalloc == kmalloc on kext land (wired kernel memory) */
#define vmalloc(size)          kmalloc((size), GFP_KERNEL)
#define vfree(p)               kfree(p)
#define vmalloc_to_page(p)     ((struct page *)NULL)   /* unused on USB path */

/* kmem_cache — vendor uses it for recv frames; back it with IOMalloc */
struct kmem_cache;
struct kmem_cache *rtl_kmem_cache_create(const char *name, size_t size);
void  rtl_kmem_cache_destroy(struct kmem_cache *c);
void *rtl_kmem_cache_alloc(struct kmem_cache *c);
void  rtl_kmem_cache_free(struct kmem_cache *c, void *obj);

#define kmem_cache_create(name, sz, align, flags, ctor) \
        rtl_kmem_cache_create((name), (sz))
#define kmem_cache_alloc(c, flags)  rtl_kmem_cache_alloc(c)
#define kmem_cache_free(c, obj)     rtl_kmem_cache_free((c), (obj))
#define kmem_cache_destroy(c)       rtl_kmem_cache_destroy(c)

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_SLAB_H_RTL8814 */
