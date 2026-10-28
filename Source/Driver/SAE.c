/*
 * RTL8814Native — standalone SAE group-19 implementation
 *
 * This translation unit has no kernel, operating-system, OpenSSL, or
 * corecrypto dependencies. It implements the legacy hunting-and-pecking
 * password element method for P-256. The same source is built by the kext
 * and the userland vector harness.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "SAE.h"

#define SAE_PWE_ROUNDS 40U
#define SAE_SHA256_LEN 32U
#define SAE_AUTH_GROUP_LEN 2U
#define SAE_COMMIT_FIXED_LEN (SAE_AUTH_GROUP_LEN + RTL_SAE_SCALAR_LEN + RTL_SAE_ELEMENT_LEN)
#define SAE_FIELD_LIMBS 4U
#define SAE_PRODUCT_LIMBS 8U

struct sae_u256 {
    uint64_t limb[SAE_FIELD_LIMBS];
};

struct sae_point {
    struct sae_u256 x;
    struct sae_u256 y;
    struct sae_u256 z;
};

struct sae_sha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
};

struct sae_iovec {
    const uint8_t *data;
    size_t length;
};

static const struct sae_u256 SAE_P = {{
    UINT64_C(0xffffffffffffffff), UINT64_C(0x00000000ffffffff),
    UINT64_C(0x0000000000000000), UINT64_C(0xffffffff00000001)
}};

static const struct sae_u256 SAE_N = {{
    UINT64_C(0xf3b9cac2fc632551), UINT64_C(0xbce6faada7179e84),
    UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffff00000000)
}};

static const struct sae_u256 SAE_P_MINUS_2 = {{
    UINT64_C(0xfffffffffffffffd), UINT64_C(0x00000000ffffffff),
    UINT64_C(0x0000000000000000), UINT64_C(0xffffffff00000001)
}};

static const struct sae_u256 SAE_SQRT_EXP = {{
    UINT64_C(0x0000000000000000), UINT64_C(0x0000000040000000),
    UINT64_C(0x4000000000000000), UINT64_C(0x3fffffffc0000000)
}};

static const struct sae_u256 SAE_B = {{
    UINT64_C(0x3bce3c3e27d2604b), UINT64_C(0x651d06b0cc53b0f6),
    UINT64_C(0xb3ebbd55769886bc), UINT64_C(0x5ac635d8aa3a93e7)
}};

static const uint32_t SAE_SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sae_secure_zero(void *memory, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)memory;
    while (length--)
        *p++ = 0;
}

static uint32_t sae_rotr32(uint32_t value, unsigned int shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static uint32_t sae_load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void sae_store_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void sae_sha256_transform(struct sae_sha256 *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int i;

    for (i = 0; i < 16; i++)
        w[i] = sae_load_be32(block + i * 4U);
    for (i = 16; i < 64; i++) {
        uint32_t s0 = sae_rotr32(w[i - 15], 7) ^
                      sae_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sae_rotr32(w[i - 2], 17) ^
                      sae_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = sae_rotr32(e, 6) ^ sae_rotr32(e, 11) ^ sae_rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + SAE_SHA256_K[i] + w[i];
        uint32_t s0 = sae_rotr32(a, 2) ^ sae_rotr32(a, 13) ^ sae_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
    sae_secure_zero(w, sizeof(w));
}

static void sae_sha256_init(struct sae_sha256 *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    unsigned int i;
    for (i = 0; i < 8; i++)
        ctx->h[i] = initial[i];
    ctx->bytes = 0;
    ctx->used = 0;
}

static void sae_sha256_update(struct sae_sha256 *ctx, const uint8_t *data, size_t length)
{
    size_t take;
    if (!length)
        return;
    ctx->bytes += (uint64_t)length;
    while (length) {
        take = sizeof(ctx->block) - ctx->used;
        if (take > length)
            take = length;
        for (size_t i = 0; i < take; i++)
            ctx->block[ctx->used + i] = data[i];
        ctx->used += take;
        data += take;
        length -= take;
        if (ctx->used == sizeof(ctx->block)) {
            sae_sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void sae_sha256_final(struct sae_sha256 *ctx, uint8_t digest[32])
{
    uint64_t bit_count = ctx->bytes * 8U;
    unsigned int i;
    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        while (ctx->used < 64)
            ctx->block[ctx->used++] = 0;
        sae_sha256_transform(ctx, ctx->block);
        ctx->used = 0;
    }
    while (ctx->used < 56)
        ctx->block[ctx->used++] = 0;
    for (i = 0; i < 8; i++)
        ctx->block[56 + i] = (uint8_t)(bit_count >> (56U - i * 8U));
    sae_sha256_transform(ctx, ctx->block);
    for (i = 0; i < 8; i++)
        sae_store_be32(digest + i * 4U, ctx->h[i]);
    sae_secure_zero(ctx, sizeof(*ctx));
}

static void sae_hmac_sha256_vector(const uint8_t *key, size_t key_length,
                                   const struct sae_iovec *parts, size_t count,
                                   uint8_t digest[32])
{
    struct sae_sha256 ctx;
    uint8_t key_block[64], inner[32];
    size_t i;
    for (i = 0; i < sizeof(key_block); i++)
        key_block[i] = 0;
    if (key_length > sizeof(key_block)) {
        sae_sha256_init(&ctx);
        sae_sha256_update(&ctx, key, key_length);
        sae_sha256_final(&ctx, key_block);
    } else {
        for (i = 0; i < key_length; i++)
            key_block[i] = key[i];
    }
    for (i = 0; i < sizeof(key_block); i++)
        key_block[i] ^= 0x36;
    sae_sha256_init(&ctx);
    sae_sha256_update(&ctx, key_block, sizeof(key_block));
    for (i = 0; i < count; i++)
        sae_sha256_update(&ctx, parts[i].data, parts[i].length);
    sae_sha256_final(&ctx, inner);
    for (i = 0; i < sizeof(key_block); i++)
        key_block[i] ^= (uint8_t)(0x36 ^ 0x5c);
    sae_sha256_init(&ctx);
    sae_sha256_update(&ctx, key_block, sizeof(key_block));
    sae_sha256_update(&ctx, inner, sizeof(inner));
    sae_sha256_final(&ctx, digest);
    sae_secure_zero(key_block, sizeof(key_block));
    sae_secure_zero(inner, sizeof(inner));
}

static void sae_hmac_sha256(const uint8_t *key, size_t key_length,
                            const uint8_t *data, size_t data_length,
                            uint8_t digest[32])
{
    struct sae_iovec part = { data, data_length };
    sae_hmac_sha256_vector(key, key_length, &part, 1, digest);
}

static void sae_kdf_hash_length(const uint8_t key[32], const char *label,
                                const uint8_t *context, size_t context_length,
                                uint8_t *output, size_t output_bits)
{
    uint16_t counter = 1;
    uint8_t count_le[2], bits_le[2], block[32];
    size_t output_length = (output_bits + 7U) / 8U;
    size_t position = 0;
    size_t label_length = 0;
    struct sae_iovec parts[4];
    while (label[label_length])
        label_length++;
    bits_le[0] = (uint8_t)output_bits;
    bits_le[1] = (uint8_t)(output_bits >> 8);
    parts[0].data = count_le; parts[0].length = sizeof(count_le);
    parts[1].data = (const uint8_t *)label; parts[1].length = label_length;
    parts[2].data = context; parts[2].length = context_length;
    parts[3].data = bits_le; parts[3].length = sizeof(bits_le);
    while (position < output_length) {
        size_t take = output_length - position;
        count_le[0] = (uint8_t)counter;
        count_le[1] = (uint8_t)(counter >> 8);
        sae_hmac_sha256_vector(key, 32, parts, 4, block);
        if (take > sizeof(block))
            take = sizeof(block);
        for (size_t i = 0; i < take; i++)
            output[position + i] = block[i];
        position += take;
        counter++;
    }
    if (output_bits & 7U)
        output[output_length - 1] &= (uint8_t)(0xffU << (8U - (output_bits & 7U)));
    sae_secure_zero(block, sizeof(block));
    sae_secure_zero(count_le, sizeof(count_le));
    sae_secure_zero(bits_le, sizeof(bits_le));
}

static void sae_u256_from_be(struct sae_u256 *value, const uint8_t input[32])
{
    unsigned int i, j;
    for (i = 0; i < SAE_FIELD_LIMBS; i++) {
        uint64_t limb = 0;
        for (j = 0; j < 8; j++)
            limb |= (uint64_t)input[31U - (i * 8U + j)] << (j * 8U);
        value->limb[i] = limb;
    }
}

static void sae_u256_to_be(const struct sae_u256 *value, uint8_t output[32])
{
    unsigned int i, j;
    for (i = 0; i < SAE_FIELD_LIMBS; i++) {
        for (j = 0; j < 8; j++)
            output[31U - (i * 8U + j)] =
                (uint8_t)(value->limb[i] >> (j * 8U));
    }
}

static uint64_t sae_u256_sub_raw(const struct sae_u256 *a,
                                 const struct sae_u256 *b,
                                 struct sae_u256 *result)
{
    uint64_t borrow = 0;
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++) {
        uint64_t ai = a->limb[i];
        uint64_t bi = b->limb[i];
        uint64_t first = ai - bi;
        uint64_t b1 = (uint64_t)(ai < bi);
        uint64_t second = first - borrow;
        uint64_t b2 = (uint64_t)(first < borrow);
        result->limb[i] = second;
        borrow = b1 | b2;
    }
    return borrow;
}

static uint64_t sae_u256_ge_mask(const struct sae_u256 *a,
                                 const struct sae_u256 *b)
{
    struct sae_u256 ignored;
    uint64_t borrow = sae_u256_sub_raw(a, b, &ignored);
    sae_secure_zero(&ignored, sizeof(ignored));
    return UINT64_C(0) - (uint64_t)(borrow == 0);
}

static uint64_t sae_u256_eq_mask(const struct sae_u256 *a,
                                 const struct sae_u256 *b)
{
    uint64_t different = 0;
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++)
        different |= a->limb[i] ^ b->limb[i];
    different |= UINT64_C(0) - different;
    return UINT64_C(0) - ((different >> 63) ^ UINT64_C(1));
}

static void sae_u256_select(struct sae_u256 *result,
                            const struct sae_u256 *a,
                            const struct sae_u256 *b, uint64_t choose_b)
{
    uint64_t mask = UINT64_C(0) - (uint64_t)(choose_b != 0);
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++)
        result->limb[i] = (a->limb[i] & ~mask) | (b->limb[i] & mask);
}

static void sae_mod_add(const struct sae_u256 *a, const struct sae_u256 *b,
                        const struct sae_u256 *modulus, struct sae_u256 *result)
{
    struct sae_u256 sum, reduced;
    uint64_t carry = 0;
    uint64_t borrow;
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++) {
        __uint128_t wide = (__uint128_t)a->limb[i] + b->limb[i] + carry;
        sum.limb[i] = (uint64_t)wide;
        carry = (uint64_t)(wide >> 64);
    }
    borrow = sae_u256_sub_raw(&sum, modulus, &reduced);
    sae_u256_select(result, &sum, &reduced, carry | (borrow ^ UINT64_C(1)));
    sae_secure_zero(&sum, sizeof(sum));
    sae_secure_zero(&reduced, sizeof(reduced));
}

static void sae_mod_sub(const struct sae_u256 *a, const struct sae_u256 *b,
                        const struct sae_u256 *modulus, struct sae_u256 *result)
{
    struct sae_u256 difference, corrected;
    uint64_t borrow = sae_u256_sub_raw(a, b, &difference);
    uint64_t carry = 0;
    uint64_t mask = UINT64_C(0) - borrow;
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++) {
        __uint128_t wide = (__uint128_t)difference.limb[i] +
                           (modulus->limb[i] & mask) + carry;
        corrected.limb[i] = (uint64_t)wide;
        carry = (uint64_t)(wide >> 64);
    }
    /* On underflow, difference + modulus wraps to the desired residue. */
    sae_u256_select(result, &difference, &corrected, borrow);
    sae_secure_zero(&difference, sizeof(difference));
    sae_secure_zero(&corrected, sizeof(corrected));
}

static void sae_mod_mul(const struct sae_u256 *a, const struct sae_u256 *b,
                        const struct sae_u256 *modulus, struct sae_u256 *result)
{
    uint64_t product[SAE_PRODUCT_LIMBS] = {0};
    struct sae_u256 remainder = {{0, 0, 0, 0}};
    struct sae_u256 shifted, reduced;

    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++) {
        uint64_t carry = 0;
        for (unsigned int j = 0; j < SAE_FIELD_LIMBS; j++) {
            __uint128_t wide = (__uint128_t)a->limb[i] * b->limb[j] +
                               product[i + j] + carry;
            product[i + j] = (uint64_t)wide;
            carry = (uint64_t)(wide >> 64);
        }
        for (unsigned int k = i + SAE_FIELD_LIMBS; k < SAE_PRODUCT_LIMBS; k++) {
            __uint128_t wide = (__uint128_t)product[k] + carry;
            product[k] = (uint64_t)wide;
            carry = (uint64_t)(wide >> 64);
        }
    }

    for (int bit = (int)(SAE_PRODUCT_LIMBS * 64U) - 1; bit >= 0; bit--) {
        uint64_t carry = (product[(unsigned int)bit / 64U] >> ((unsigned int)bit & 63U)) & 1U;
        uint64_t borrow;
        for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++) {
            uint64_t next = remainder.limb[i] >> 63;
            shifted.limb[i] = (remainder.limb[i] << 1) | carry;
            carry = next;
        }
        borrow = sae_u256_sub_raw(&shifted, modulus, &reduced);
        sae_u256_select(&remainder, &shifted, &reduced,
                        carry | (borrow ^ UINT64_C(1)));
    }
    *result = remainder;
    sae_secure_zero(product, sizeof(product));
    sae_secure_zero(&remainder, sizeof(remainder));
    sae_secure_zero(&shifted, sizeof(shifted));
    sae_secure_zero(&reduced, sizeof(reduced));
}

static void sae_fp_add(const struct sae_u256 *a, const struct sae_u256 *b,
                       struct sae_u256 *result)
{
    sae_mod_add(a, b, &SAE_P, result);
}

static void sae_fp_sub(const struct sae_u256 *a, const struct sae_u256 *b,
                       struct sae_u256 *result)
{
    sae_mod_sub(a, b, &SAE_P, result);
}

static void sae_fp_mul(const struct sae_u256 *a, const struct sae_u256 *b,
                       struct sae_u256 *result)
{
    sae_mod_mul(a, b, &SAE_P, result);
}

static void sae_fp_square(const struct sae_u256 *a, struct sae_u256 *result)
{
    sae_fp_mul(a, a, result);
}

static uint64_t sae_u256_bit(const struct sae_u256 *value, unsigned int bit)
{
    return (value->limb[bit / 64U] >> (bit & 63U)) & 1U;
}

static void sae_fp_pow(const struct sae_u256 *base, const struct sae_u256 *exponent,
                       struct sae_u256 *result)
{
    struct sae_u256 accumulator = {{1, 0, 0, 0}};
    struct sae_u256 squared, multiplied;
    for (int bit = 255; bit >= 0; bit--) {
        uint64_t select_multiply;
        sae_fp_square(&accumulator, &squared);
        sae_fp_mul(&squared, base, &multiplied);
        select_multiply = sae_u256_bit(exponent, (unsigned int)bit);
        sae_u256_select(&accumulator, &squared, &multiplied, select_multiply);
    }
    *result = accumulator;
    sae_secure_zero(&accumulator, sizeof(accumulator));
    sae_secure_zero(&squared, sizeof(squared));
    sae_secure_zero(&multiplied, sizeof(multiplied));
}

static uint64_t sae_u256_is_zero(const struct sae_u256 *value)
{
    uint64_t combined = 0;
    for (unsigned int i = 0; i < SAE_FIELD_LIMBS; i++)
        combined |= value->limb[i];
    combined |= UINT64_C(0) - combined;
    return (combined >> 63) ^ UINT64_C(1);
}

static void sae_fp_negate(const struct sae_u256 *a, struct sae_u256 *result)
{
    struct sae_u256 zero = {{0, 0, 0, 0}};
    sae_fp_sub(&zero, a, result);
}

static void sae_point_infinity(struct sae_point *point)
{
    sae_secure_zero(point, sizeof(*point));
}

static uint64_t sae_point_is_infinity(const struct sae_point *point)
{
    return sae_u256_is_zero(&point->z);
}

static void sae_point_select(struct sae_point *result,
                             const struct sae_point *a,
                             const struct sae_point *b, uint64_t choose_b)
{
    sae_u256_select(&result->x, &a->x, &b->x, choose_b);
    sae_u256_select(&result->y, &a->y, &b->y, choose_b);
    sae_u256_select(&result->z, &a->z, &b->z, choose_b);
}

static void sae_point_cswap(struct sae_point *a, struct sae_point *b, uint64_t swap)
{
    struct sae_point temporary;
    sae_point_select(&temporary, a, b, swap);
    sae_point_select(b, b, a, swap);
    *a = temporary;
    sae_secure_zero(&temporary, sizeof(temporary));
}

static void sae_point_double(const struct sae_point *point, struct sae_point *result)
{
    struct sae_u256 delta, gamma, beta, alpha, tmp1, tmp2;
    struct sae_u256 x3, y3, z3, gamma2;
    struct sae_u256 three = {{3, 0, 0, 0}};
    struct sae_u256 four = {{4, 0, 0, 0}}, eight = {{8, 0, 0, 0}};

    sae_fp_square(&point->z, &delta);
    sae_fp_square(&point->y, &gamma);
    sae_fp_mul(&point->x, &gamma, &beta);
    sae_fp_sub(&point->x, &delta, &tmp1);
    sae_fp_add(&point->x, &delta, &tmp2);
    sae_fp_mul(&tmp1, &tmp2, &alpha);
    sae_fp_mul(&alpha, &three, &alpha);
    sae_fp_square(&alpha, &x3);
    sae_fp_mul(&beta, &eight, &tmp1);
    sae_fp_sub(&x3, &tmp1, &x3);
    sae_fp_add(&point->y, &point->z, &z3);
    sae_fp_square(&z3, &z3);
    sae_fp_sub(&z3, &gamma, &z3);
    sae_fp_sub(&z3, &delta, &z3);
    sae_fp_mul(&beta, &four, &tmp1);
    sae_fp_sub(&tmp1, &x3, &tmp1);
    sae_fp_mul(&alpha, &tmp1, &y3);
    sae_fp_square(&gamma, &gamma2);
    sae_fp_mul(&gamma2, &eight, &tmp2);
    sae_fp_sub(&y3, &tmp2, &y3);

    result->x = x3;
    result->y = y3;
    result->z = z3;
    sae_secure_zero(&delta, sizeof(delta)); sae_secure_zero(&gamma, sizeof(gamma));
    sae_secure_zero(&beta, sizeof(beta)); sae_secure_zero(&alpha, sizeof(alpha));
    sae_secure_zero(&tmp1, sizeof(tmp1)); sae_secure_zero(&tmp2, sizeof(tmp2));
    sae_secure_zero(&x3, sizeof(x3)); sae_secure_zero(&y3, sizeof(y3));
    sae_secure_zero(&z3, sizeof(z3)); sae_secure_zero(&gamma2, sizeof(gamma2));
}

static void sae_point_add(const struct sae_point *p, const struct sae_point *q,
                          struct sae_point *result)
{
    struct sae_u256 z1z1, z2z2, u1, u2, s1, s2, h, i, j, r, v;
    struct sae_u256 x3, y3, z3, t1, two = {{2, 0, 0, 0}};
    struct sae_point general, doubled, infinity, selected;
    uint64_t h_zero, s_zero, p_inf, q_inf;

    sae_fp_square(&p->z, &z1z1);
    sae_fp_square(&q->z, &z2z2);
    sae_fp_mul(&p->x, &z2z2, &u1);
    sae_fp_mul(&q->x, &z1z1, &u2);
    sae_fp_mul(&q->z, &z2z2, &t1);
    sae_fp_mul(&p->y, &t1, &s1);
    sae_fp_mul(&p->z, &z1z1, &t1);
    sae_fp_mul(&q->y, &t1, &s2);
    sae_fp_sub(&u2, &u1, &h);
    sae_fp_mul(&h, &two, &i);
    sae_fp_square(&i, &i);
    sae_fp_mul(&h, &i, &j);
    sae_fp_sub(&s2, &s1, &r);
    sae_fp_mul(&r, &two, &r);
    sae_fp_mul(&u1, &i, &v);
    sae_fp_square(&r, &x3);
    sae_fp_sub(&x3, &j, &x3);
    sae_fp_mul(&v, &two, &t1);
    sae_fp_sub(&x3, &t1, &x3);
    sae_fp_sub(&v, &x3, &t1);
    sae_fp_mul(&r, &t1, &y3);
    sae_fp_mul(&s1, &j, &t1);
    sae_fp_mul(&t1, &two, &t1);
    sae_fp_sub(&y3, &t1, &y3);
    sae_fp_add(&p->z, &q->z, &z3);
    sae_fp_square(&z3, &z3);
    sae_fp_sub(&z3, &z1z1, &z3);
    sae_fp_sub(&z3, &z2z2, &z3);
    sae_fp_mul(&z3, &h, &z3);
    general.x = x3; general.y = y3; general.z = z3;

    sae_point_double(p, &doubled);
    sae_point_infinity(&infinity);
    h_zero = sae_u256_is_zero(&h);
    s_zero = sae_u256_is_zero(&r);
    p_inf = sae_point_is_infinity(p);
    q_inf = sae_point_is_infinity(q);
    selected = general;
    sae_point_select(&selected, &selected, &doubled, h_zero & s_zero);
    sae_point_select(&selected, &selected, &infinity, h_zero & (s_zero ^ 1U));
    sae_point_select(&selected, &selected, q, p_inf);
    sae_point_select(&selected, &selected, p, q_inf);
    *result = selected;

    sae_secure_zero(&z1z1, sizeof(z1z1)); sae_secure_zero(&z2z2, sizeof(z2z2));
    sae_secure_zero(&u1, sizeof(u1)); sae_secure_zero(&u2, sizeof(u2));
    sae_secure_zero(&s1, sizeof(s1)); sae_secure_zero(&s2, sizeof(s2));
    sae_secure_zero(&h, sizeof(h)); sae_secure_zero(&i, sizeof(i));
    sae_secure_zero(&j, sizeof(j)); sae_secure_zero(&r, sizeof(r));
    sae_secure_zero(&v, sizeof(v)); sae_secure_zero(&x3, sizeof(x3));
    sae_secure_zero(&y3, sizeof(y3)); sae_secure_zero(&z3, sizeof(z3));
    sae_secure_zero(&t1, sizeof(t1));
    sae_secure_zero(&general, sizeof(general)); sae_secure_zero(&doubled, sizeof(doubled));
    sae_secure_zero(&infinity, sizeof(infinity)); sae_secure_zero(&selected, sizeof(selected));
    sae_secure_zero(&two, sizeof(two));
}

static void sae_point_scalar_mul(const struct sae_point *point,
                                 const struct sae_u256 *scalar,
                                 struct sae_point *result)
{
    struct sae_point r0, r1, sum, doubled;
    sae_point_infinity(&r0);
    r1 = *point;
    for (int bit = 255; bit >= 0; bit--) {
        uint64_t value = sae_u256_bit(scalar, (unsigned int)bit);
        sae_point_cswap(&r0, &r1, value);
        sae_point_add(&r0, &r1, &sum);
        sae_point_double(&r0, &doubled);
        r1 = sum;
        r0 = doubled;
        sae_point_cswap(&r0, &r1, value);
    }
    *result = r0;
    sae_secure_zero(&r0, sizeof(r0)); sae_secure_zero(&r1, sizeof(r1));
    sae_secure_zero(&sum, sizeof(sum)); sae_secure_zero(&doubled, sizeof(doubled));
}

static int sae_point_to_affine(const struct sae_point *point,
                               struct sae_u256 *x, struct sae_u256 *y)
{
    struct sae_u256 inverse, inverse2, inverse3;
    if (sae_point_is_infinity(point))
        return -1;
    sae_fp_pow(&point->z, &SAE_P_MINUS_2, &inverse);
    sae_fp_square(&inverse, &inverse2);
    sae_fp_mul(&inverse2, &inverse, &inverse3);
    sae_fp_mul(&point->x, &inverse2, x);
    sae_fp_mul(&point->y, &inverse3, y);
    sae_secure_zero(&inverse, sizeof(inverse)); sae_secure_zero(&inverse2, sizeof(inverse2));
    sae_secure_zero(&inverse3, sizeof(inverse3));
    return 0;
}

static int sae_point_from_affine(const struct sae_u256 *x, const struct sae_u256 *y,
                                 struct sae_point *point)
{
    struct sae_u256 x2, x3, three_x, rhs, y2;
    struct sae_u256 three = {{3, 0, 0, 0}};
    uint64_t in_range = ((sae_u256_ge_mask(x, &SAE_P) |
                          sae_u256_ge_mask(y, &SAE_P)) & 1U) ^ 1U;
    sae_fp_square(x, &x2);
    sae_fp_mul(&x2, x, &x3);
    sae_fp_mul(x, &three, &three_x);
    sae_fp_sub(&x3, &three_x, &rhs);
    sae_fp_add(&rhs, &SAE_B, &rhs);
    sae_fp_square(y, &y2);
    if (!in_range || sae_u256_eq_mask(&rhs, &y2) != UINT64_MAX) {
        sae_secure_zero(&x2, sizeof(x2)); sae_secure_zero(&x3, sizeof(x3));
        sae_secure_zero(&three_x, sizeof(three_x)); sae_secure_zero(&rhs, sizeof(rhs));
        sae_secure_zero(&y2, sizeof(y2)); sae_secure_zero(&three, sizeof(three));
        return -1;
    }
    point->x = *x;
    point->y = *y;
    point->z.limb[0] = 1;
    point->z.limb[1] = point->z.limb[2] = point->z.limb[3] = 0;
    sae_secure_zero(&x2, sizeof(x2)); sae_secure_zero(&x3, sizeof(x3));
    sae_secure_zero(&three_x, sizeof(three_x)); sae_secure_zero(&rhs, sizeof(rhs));
    sae_secure_zero(&y2, sizeof(y2)); sae_secure_zero(&three, sizeof(three));
    return 0;
}

static void sae_point_negate(struct sae_point *point)
{
    struct sae_u256 negative_y;
    sae_fp_negate(&point->y, &negative_y);
    point->y = negative_y;
    sae_secure_zero(&negative_y, sizeof(negative_y));
}

static uint64_t sae_bytes_eq_mask(const uint8_t *a, const uint8_t *b, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; i++)
        difference |= (uint8_t)(a[i] ^ b[i]);
    difference |= (uint8_t)(0U - difference);
    return UINT64_C(0) - (uint64_t)(((difference >> 7) ^ 1U) & 1U);
}

static void sae_bytes_select(uint8_t *output, const uint8_t *a,
                             const uint8_t *b, size_t length, uint64_t choose_b)
{
    uint8_t mask = (uint8_t)(0U - (uint8_t)(choose_b != 0));
    for (size_t i = 0; i < length; i++)
        output[i] = (uint8_t)((a[i] & (uint8_t)~mask) | (b[i] & mask));
}

static void sae_point_store_affine(const struct sae_point *point, uint8_t output[64])
{
    struct sae_u256 x, y;
    (void)sae_point_to_affine(point, &x, &y);
    sae_u256_to_be(&x, output);
    sae_u256_to_be(&y, output + 32);
    sae_secure_zero(&x, sizeof(x)); sae_secure_zero(&y, sizeof(y));
}

static int sae_derive_pwe(struct rtl_sae_p256 *sae,
                          const uint8_t *password, size_t password_len,
                          const uint8_t own_address[6],
                          const uint8_t peer_address[6],
                          const uint8_t dummy_password[RTL_SAE_PASSWORD_MAX_LEN],
                          const uint8_t dummy_id[RTL_SAE_PASSWORD_ID_MAX_LEN])
{
    uint8_t address_key[12], seed[32], candidate_bytes[32], selected_x[32] = {0};
    uint8_t selected_y[32] = {0}, seed_input_counter, prime[32];
    uint8_t selected_password[RTL_SAE_PASSWORD_MAX_LEN];
    uint8_t selected_id[RTL_SAE_PASSWORD_ID_MAX_LEN];
    uint8_t kdf_output[32];
    struct sae_iovec seed_parts[3];
    struct sae_u256 x, x_safe, x2, x3, three_x, rhs, y, y_check;
    struct sae_u256 y_negative, three = {{3, 0, 0, 0}};
    uint64_t found = 0;
    size_t position = 0;

    {
        int comparison = 0;
        for (size_t i = 0; i < 6 && comparison == 0; i++)
            comparison = (own_address[i] > peer_address[i]) ? 1 :
                         (own_address[i] < peer_address[i]) ? -1 : 0;
        if (comparison >= 0) {
            for (size_t i = 0; i < 6; i++) {
                address_key[i] = own_address[i];
                address_key[i + 6] = peer_address[i];
            }
        } else {
            for (size_t i = 0; i < 6; i++) {
                address_key[i] = peer_address[i];
                address_key[i + 6] = own_address[i];
            }
        }
    }

    sae_u256_to_be(&SAE_P, prime);
    seed_parts[0].data = selected_password;
    seed_parts[0].length = password_len;
    seed_parts[1].data = selected_id;
    seed_parts[1].length = sae->password_id_len;
    seed_parts[2].data = &seed_input_counter;
    seed_parts[2].length = 1;

    for (unsigned int counter = 1; counter <= 200 &&
         (counter <= SAE_PWE_ROUNDS || !found); counter++) {
        uint64_t x_valid, sqrt_valid, take;
        seed_input_counter = (uint8_t)counter;
        sae_bytes_select(selected_password, password, dummy_password,
                         password_len, found);
        sae_bytes_select(selected_id, sae->password_id, dummy_id,
                         sae->password_id_len, found);
        sae_hmac_sha256_vector(address_key, sizeof(address_key), seed_parts, 3, seed);
        sae_kdf_hash_length(seed, "SAE Hunting and Pecking",
                            prime, sizeof(prime), kdf_output, 256);
        for (size_t i = 0; i < 32; i++)
            candidate_bytes[i] = kdf_output[i];
        sae_u256_from_be(&x, candidate_bytes);
        x_valid = (sae_u256_ge_mask(&x, &SAE_P) ^ 1U) & 1U;
        sae_u256_select(&x_safe, &x, &(struct sae_u256){{0,0,0,0}}, x_valid ^ 1U);

        sae_fp_square(&x_safe, &x2);
        sae_fp_mul(&x2, &x_safe, &x3);
        sae_fp_mul(&x_safe, &three, &three_x);
        sae_fp_sub(&x3, &three_x, &rhs);
        sae_fp_add(&rhs, &SAE_B, &rhs);
        sae_fp_pow(&rhs, &SAE_SQRT_EXP, &y);
        sae_fp_square(&y, &y_check);
        sqrt_valid = sae_u256_eq_mask(&rhs, &y_check) & 1U;
        sae_fp_negate(&y, &y_negative);
        {
            uint64_t parity_difference = (y.limb[0] & 1U) ^ (seed[31] & 1U);
            sae_u256_select(&y, &y, &y_negative, parity_difference);
        }
        take = x_valid & sqrt_valid & (sae_u256_is_zero(&y) ^ 1U) & (found ^ 1U);
        sae_u256_to_be(&x_safe, candidate_bytes);
        sae_u256_to_be(&y, kdf_output);
        sae_bytes_select(selected_x, selected_x, candidate_bytes, 32, take);
        sae_bytes_select(selected_y, selected_y, kdf_output, 32, take);
        found |= x_valid & sqrt_valid & (sae_u256_is_zero(&y) ^ 1U);
    }

    if (found) {
        for (size_t i = 0; i < 32; i++) {
            sae->pwe_x[i] = selected_x[i];
            sae->pwe_y[i] = selected_y[i];
        }
        position = 1;
    }
    sae_secure_zero(address_key, sizeof(address_key));
    sae_secure_zero(seed, sizeof(seed)); sae_secure_zero(candidate_bytes, sizeof(candidate_bytes));
    sae_secure_zero(selected_x, sizeof(selected_x)); sae_secure_zero(selected_y, sizeof(selected_y));
    sae_secure_zero(kdf_output, sizeof(kdf_output)); sae_secure_zero(&x, sizeof(x));
    sae_secure_zero(&x_safe, sizeof(x_safe)); sae_secure_zero(&x2, sizeof(x2));
    sae_secure_zero(&x3, sizeof(x3)); sae_secure_zero(&three_x, sizeof(three_x));
    sae_secure_zero(&rhs, sizeof(rhs));
    sae_secure_zero(&y, sizeof(y)); sae_secure_zero(&y_check, sizeof(y_check));
    sae_secure_zero(&y_negative, sizeof(y_negative));
    sae_secure_zero(&three, sizeof(three)); sae_secure_zero(prime, sizeof(prime));
    sae_secure_zero(selected_password, sizeof(selected_password));
    sae_secure_zero(selected_id, sizeof(selected_id));
    return position ? 0 : -1;
}

static int sae_commit_scalar_valid(const struct sae_u256 *value)
{
    uint64_t greater_than_one = sae_u256_ge_mask(value, &(struct sae_u256){{2,0,0,0}}) & 1U;
    uint64_t below_order = sae_u256_ge_mask(value, &SAE_N) ^ 1U;
    return (int)(greater_than_one & below_order);
}

static int sae_set_commit_values(struct rtl_sae_p256 *sae,
                                 const uint8_t rand_bytes[32],
                                 const uint8_t mask_bytes[32])
{
    struct sae_u256 rand_value, mask_value, scalar, pwe_x, pwe_y;
    struct sae_point pwe, element;
    uint8_t scalar_bytes[32];
    sae_u256_from_be(&rand_value, rand_bytes);
    sae_u256_from_be(&mask_value, mask_bytes);
    if (!sae_commit_scalar_valid(&rand_value) || !sae_commit_scalar_valid(&mask_value))
        goto fail;
    sae_mod_add(&rand_value, &mask_value, &SAE_N, &scalar);
    if (!sae_commit_scalar_valid(&scalar))
        goto fail;
    pwe_x = (struct sae_u256){{0,0,0,0}};
    pwe_y = (struct sae_u256){{0,0,0,0}};
    sae_u256_from_be(&pwe_x, sae->pwe_x);
    sae_u256_from_be(&pwe_y, sae->pwe_y);
    if (sae_point_from_affine(&pwe_x, &pwe_y, &pwe) < 0)
        goto fail;
    sae_point_scalar_mul(&pwe, &mask_value, &element);
    sae_point_negate(&element);
    if (sae_point_is_infinity(&element))
        goto fail;
    sae_u256_to_be(&scalar, scalar_bytes);
    for (size_t i = 0; i < 32; i++) {
        sae->rand[i] = rand_bytes[i];
        sae->own_scalar[i] = scalar_bytes[i];
    }
    sae_point_store_affine(&element, sae->own_element);
    sae_secure_zero(&rand_value, sizeof(rand_value)); sae_secure_zero(&mask_value, sizeof(mask_value));
    sae_secure_zero(&scalar, sizeof(scalar)); sae_secure_zero(&pwe_x, sizeof(pwe_x));
    sae_secure_zero(&pwe_y, sizeof(pwe_y)); sae_secure_zero(&pwe, sizeof(pwe));
    sae_secure_zero(&element, sizeof(element)); sae_secure_zero(scalar_bytes, sizeof(scalar_bytes));
    return 0;
fail:
    sae_secure_zero(&rand_value, sizeof(rand_value)); sae_secure_zero(&mask_value, sizeof(mask_value));
    sae_secure_zero(&scalar, sizeof(scalar)); sae_secure_zero(&pwe_x, sizeof(pwe_x));
    sae_secure_zero(&pwe_y, sizeof(pwe_y)); sae_secure_zero(&pwe, sizeof(pwe));
    sae_secure_zero(&element, sizeof(element)); sae_secure_zero(scalar_bytes, sizeof(scalar_bytes));
    return -1;
}

static int sae_start_common(struct rtl_sae_p256 *sae,
                            const uint8_t *password, size_t password_len,
                            const uint8_t own_address[6],
                            const uint8_t peer_address[6],
                            const uint8_t *password_id, size_t password_id_len,
                            const uint8_t rand[32], const uint8_t mask[32],
                            const uint8_t dummy_password[RTL_SAE_PASSWORD_MAX_LEN],
                            const uint8_t dummy_id[RTL_SAE_PASSWORD_ID_MAX_LEN])
{
    if (!sae || !password || password_len == 0 ||
        password_len > RTL_SAE_PASSWORD_MAX_LEN || !own_address || !peer_address ||
        (password_id_len && !password_id) ||
        password_id_len > RTL_SAE_PASSWORD_ID_MAX_LEN || !rand || !mask ||
        !dummy_password || !dummy_id) {
        if (sae)
            rtl_sae_p256_clear(sae);
        return -1;
    }
    rtl_sae_p256_clear(sae);
    sae->password_id_len = (uint8_t)password_id_len;
    for (size_t i = 0; i < password_id_len; i++)
        sae->password_id[i] = password_id[i];
    if (sae_derive_pwe(sae, password, password_len, own_address, peer_address,
                       dummy_password, dummy_id) < 0 ||
        sae_set_commit_values(sae, rand, mask) < 0) {
        rtl_sae_p256_clear(sae);
        return -1;
    }
    sae->state = RTL_SAE_COMMITTED;
    sae->send_confirm = 1;
    return 0;
}

#ifdef RTL_SAE_TESTING
int rtl_sae_p256_start_fixed(struct rtl_sae_p256 *sae,
                             const uint8_t *password, size_t password_len,
                             const uint8_t own_address[6],
                             const uint8_t peer_address[6],
                             const uint8_t *password_id, size_t password_id_len,
                             const uint8_t rand[32], const uint8_t mask[32])
{
    const uint8_t dummy_password[RTL_SAE_PASSWORD_MAX_LEN] = {0};
    const uint8_t dummy_id[RTL_SAE_PASSWORD_ID_MAX_LEN] = {0};
    return sae_start_common(sae, password, password_len, own_address, peer_address,
                            password_id, password_id_len, rand, mask,
                            dummy_password, dummy_id);
}
#endif

int rtl_sae_p256_start(struct rtl_sae_p256 *sae,
                       const uint8_t *password, size_t password_len,
                       const uint8_t own_address[6], const uint8_t peer_address[6],
                       const uint8_t *password_id, size_t password_id_len,
                       rtl_sae_random_fn random_bytes, void *random_context)
{
    uint8_t rand[32], mask[32];
    uint8_t dummy_password[RTL_SAE_PASSWORD_MAX_LEN];
    uint8_t dummy_id[RTL_SAE_PASSWORD_ID_MAX_LEN];
    struct sae_u256 value;
    int result = -1;
    if (!sae || !random_bytes) {
        if (sae)
            rtl_sae_p256_clear(sae);
        return -1;
    }
    if (random_bytes(random_context, dummy_password, sizeof(dummy_password)) != 0 ||
        random_bytes(random_context, dummy_id, sizeof(dummy_id)) != 0)
        goto done;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        if (random_bytes(random_context, rand, sizeof(rand)) != 0 ||
            random_bytes(random_context, mask, sizeof(mask)) != 0)
            break;
        sae_u256_from_be(&value, rand);
        if (!sae_commit_scalar_valid(&value))
            continue;
        sae_u256_from_be(&value, mask);
        if (!sae_commit_scalar_valid(&value))
            continue;
        sae_u256_from_be(&value, rand);
        {
            struct sae_u256 mask_value, sum;
            int valid_sum;
            sae_u256_from_be(&mask_value, mask);
            sae_mod_add(&value, &mask_value, &SAE_N, &sum);
            valid_sum = sae_commit_scalar_valid(&sum);
            sae_secure_zero(&mask_value, sizeof(mask_value));
            sae_secure_zero(&sum, sizeof(sum));
            if (!valid_sum)
                continue;
        }
        result = sae_start_common(sae, password, password_len, own_address, peer_address,
                                  password_id, password_id_len, rand, mask,
                                  dummy_password, dummy_id);
        break;
    }
done:
    if (result < 0)
        rtl_sae_p256_clear(sae);
    sae_secure_zero(rand, sizeof(rand)); sae_secure_zero(mask, sizeof(mask));
    sae_secure_zero(dummy_password, sizeof(dummy_password));
    sae_secure_zero(dummy_id, sizeof(dummy_id));
    sae_secure_zero(&value, sizeof(value));
    return result;
}

int rtl_sae_p256_set_token(struct rtl_sae_p256 *sae,
                           const uint8_t *token, size_t token_len)
{
    if (!sae || sae->state != RTL_SAE_COMMITTED ||
        token_len == 0 || token_len > RTL_SAE_TOKEN_MAX_LEN || !token)
        return -1;
    sae_secure_zero(sae->token, sizeof(sae->token));
    for (size_t i = 0; i < token_len; i++)
        sae->token[i] = token[i];
    sae->token_len = (uint16_t)token_len;
    return 0;
}

int rtl_sae_p256_write_commit(const struct rtl_sae_p256 *sae,
                              uint8_t *output, size_t output_capacity,
                              size_t *output_len)
{
    size_t length;
    size_t position = 0;
    if (output_len)
        *output_len = 0;
    if (!sae || !output || !output_len || sae->state != RTL_SAE_COMMITTED)
        return -1;
    length = SAE_COMMIT_FIXED_LEN + sae->token_len;
    if (sae->password_id_len)
        length += 3U + sae->password_id_len;
    if (length > output_capacity || length > RTL_SAE_COMMIT_MAX_LEN)
        return -1;
    output[position++] = (uint8_t)RTL_SAE_GROUP_P256;
    output[position++] = 0;
    for (size_t i = 0; i < sae->token_len; i++)
        output[position++] = sae->token[i];
    for (size_t i = 0; i < RTL_SAE_SCALAR_LEN; i++)
        output[position++] = sae->own_scalar[i];
    for (size_t i = 0; i < RTL_SAE_ELEMENT_LEN; i++)
        output[position++] = sae->own_element[i];
    if (sae->password_id_len) {
        output[position++] = 255;
        output[position++] = (uint8_t)(1U + sae->password_id_len);
        output[position++] = 33;
        for (size_t i = 0; i < sae->password_id_len; i++)
            output[position++] = sae->password_id[i];
    }
    *output_len = position;
    return 0;
}

static int sae_derive_keys(struct rtl_sae_p256 *sae, const uint8_t shared_x[32])
{
    uint8_t zero_key[32] = {0}, keyseed[32], scalar_sum_bytes[32], keys[64];
    struct sae_u256 own, peer, sum;
    struct sae_iovec context;
    sae_hmac_sha256(zero_key, sizeof(zero_key), shared_x, 32, keyseed);
    sae_u256_from_be(&own, sae->own_scalar);
    sae_u256_from_be(&peer, sae->peer_scalar);
    sae_mod_add(&own, &peer, &SAE_N, &sum);
    sae_u256_to_be(&sum, scalar_sum_bytes);
    context.data = scalar_sum_bytes;
    context.length = sizeof(scalar_sum_bytes);
    sae_kdf_hash_length(keyseed, "SAE KCK and PMK", context.data,
                        context.length, keys, sizeof(keys) * 8U);
    for (size_t i = 0; i < RTL_SAE_KCK_LEN; i++)
        sae->kck[i] = keys[i];
    for (size_t i = 0; i < RTL_SAE_PMK_LEN; i++)
        sae->pmk[i] = keys[RTL_SAE_KCK_LEN + i];
    for (size_t i = 0; i < RTL_SAE_PMKID_LEN; i++)
        sae->pmkid[i] = scalar_sum_bytes[i];
    sae_secure_zero(zero_key, sizeof(zero_key)); sae_secure_zero(keyseed, sizeof(keyseed));
    sae_secure_zero(scalar_sum_bytes, sizeof(scalar_sum_bytes)); sae_secure_zero(keys, sizeof(keys));
    sae_secure_zero(&own, sizeof(own)); sae_secure_zero(&peer, sizeof(peer));
    sae_secure_zero(&sum, sizeof(sum));
    return 0;
}

int rtl_sae_p256_process_commit(struct rtl_sae_p256 *sae,
                                const uint8_t *body, size_t body_len,
                                size_t peer_token_len)
{
    size_t position;
    struct sae_u256 peer_scalar, peer_x, peer_y, pwe_x, pwe_y, rand_value, shared_x, shared_y;
    struct sae_point pwe, peer_element, intermediate, shared;
    uint8_t shared_x_bytes[32];
    uint64_t reflection;
    int result = -1;

    if (!sae || !body || sae->state != RTL_SAE_COMMITTED ||
        body_len < SAE_COMMIT_FIXED_LEN ||
        peer_token_len > body_len - SAE_COMMIT_FIXED_LEN)
        goto fail;
    if (body[0] != RTL_SAE_GROUP_P256 || body[1] != 0)
        goto fail;
    position = SAE_AUTH_GROUP_LEN + peer_token_len;
    sae_u256_from_be(&peer_scalar, body + position);
    if (!sae_commit_scalar_valid(&peer_scalar))
        goto fail;
    position += 32;
    sae_u256_from_be(&peer_x, body + position);
    sae_u256_from_be(&peer_y, body + position + 32);
    if (sae_point_from_affine(&peer_x, &peer_y, &peer_element) < 0)
        goto fail;
    position += 64;
    if (sae->password_id_len) {
        uint8_t difference = 0;
        if (body_len - position != 3U + sae->password_id_len ||
            body[position] != 255 || body[position + 1] != 1U + sae->password_id_len ||
            body[position + 2] != 33)
            goto fail;
        for (size_t i = 0; i < sae->password_id_len; i++)
            difference |= (uint8_t)(body[position + 3U + i] ^ sae->password_id[i]);
        if (difference)
            goto fail;
    } else if (body_len != position) {
        goto fail;
    }

    reflection = sae_bytes_eq_mask(sae->own_scalar, body + SAE_AUTH_GROUP_LEN + peer_token_len, 32) & 1U;
    reflection &= sae_bytes_eq_mask(sae->own_element,
                                    body + SAE_AUTH_GROUP_LEN + peer_token_len + 32, 64) & 1U;
    if (reflection)
        goto fail;

    sae_u256_from_be(&pwe_x, sae->pwe_x);
    sae_u256_from_be(&pwe_y, sae->pwe_y);
    sae_u256_from_be(&rand_value, sae->rand);
    if (sae_point_from_affine(&pwe_x, &pwe_y, &pwe) < 0)
        goto fail;
    sae_point_scalar_mul(&pwe, &peer_scalar, &intermediate);
    sae_point_add(&intermediate, &peer_element, &intermediate);
    sae_point_scalar_mul(&intermediate, &rand_value, &shared);
    if (sae_point_to_affine(&shared, &shared_x, &shared_y) < 0)
        goto fail;
    sae_u256_to_be(&shared_x, shared_x_bytes);
    for (size_t i = 0; i < 32; i++)
        sae->peer_scalar[i] = body[SAE_AUTH_GROUP_LEN + peer_token_len + i];
    for (size_t i = 0; i < 64; i++)
        sae->peer_element[i] = body[SAE_AUTH_GROUP_LEN + peer_token_len + 32U + i];
    (void)sae_derive_keys(sae, shared_x_bytes);
    sae->state = RTL_SAE_PEER_COMMITTED;
    result = 0;
    goto cleanup;
fail:
    if (sae)
        rtl_sae_p256_clear(sae);
cleanup:
    sae_secure_zero(&peer_scalar, sizeof(peer_scalar)); sae_secure_zero(&peer_x, sizeof(peer_x));
    sae_secure_zero(&peer_y, sizeof(peer_y)); sae_secure_zero(&pwe_x, sizeof(pwe_x));
    sae_secure_zero(&pwe_y, sizeof(pwe_y)); sae_secure_zero(&rand_value, sizeof(rand_value));
    sae_secure_zero(&shared_x, sizeof(shared_x)); sae_secure_zero(&shared_y, sizeof(shared_y));
    sae_secure_zero(&pwe, sizeof(pwe)); sae_secure_zero(&peer_element, sizeof(peer_element));
    sae_secure_zero(&intermediate, sizeof(intermediate)); sae_secure_zero(&shared, sizeof(shared));
    sae_secure_zero(shared_x_bytes, sizeof(shared_x_bytes));
    return result;
}

static void sae_make_confirm(const struct rtl_sae_p256 *sae,
                             const uint8_t counter_le[2],
                             const uint8_t scalar1[32], const uint8_t element1[64],
                             const uint8_t scalar2[32], const uint8_t element2[64],
                             uint8_t verifier[32])
{
    struct sae_iovec parts[5];
    parts[0].data = counter_le; parts[0].length = 2;
    parts[1].data = scalar1; parts[1].length = 32;
    parts[2].data = element1; parts[2].length = 64;
    parts[3].data = scalar2; parts[3].length = 32;
    parts[4].data = element2; parts[4].length = 64;
    sae_hmac_sha256_vector(sae->kck, sizeof(sae->kck), parts, 5, verifier);
}

int rtl_sae_p256_write_confirm(struct rtl_sae_p256 *sae,
                               uint8_t output[RTL_SAE_CONFIRM_LEN])
{
    uint8_t verifier[32];
    if (!sae || !output || sae->state != RTL_SAE_PEER_COMMITTED)
        return -1;
    output[0] = (uint8_t)sae->send_confirm;
    output[1] = (uint8_t)(sae->send_confirm >> 8);
    sae_make_confirm(sae, output, sae->own_scalar, sae->own_element,
                     sae->peer_scalar, sae->peer_element, verifier);
    for (size_t i = 0; i < sizeof(verifier); i++)
        output[2 + i] = verifier[i];
    if (sae->send_confirm < UINT16_MAX)
        sae->send_confirm++;
    sae->confirm_sent = 1;
    sae_secure_zero(verifier, sizeof(verifier));
    return 0;
}

int rtl_sae_p256_check_confirm(struct rtl_sae_p256 *sae,
                               const uint8_t *body, size_t body_len)
{
    uint8_t verifier[32];
    uint16_t counter;
    uint64_t equal;
    if (!sae || !body || body_len != RTL_SAE_CONFIRM_LEN ||
        sae->state != RTL_SAE_PEER_COMMITTED)
        return -1;
    counter = (uint16_t)(body[0] | ((uint16_t)body[1] << 8));
    if (sae->peer_confirm_seen &&
        (sae->peer_confirm == UINT16_MAX || counter <= sae->peer_confirm))
        return -1;
    sae_make_confirm(sae, body, sae->peer_scalar, sae->peer_element,
                     sae->own_scalar, sae->own_element, verifier);
    equal = sae_bytes_eq_mask(verifier, body + 2, sizeof(verifier));
    sae_secure_zero(verifier, sizeof(verifier));
    if (equal != UINT64_MAX)
        return -1;
    sae->peer_confirm = counter;
    sae->peer_confirm_seen = 1;
    return 0;
}

int rtl_sae_p256_finish(struct rtl_sae_p256 *sae,
                        uint8_t pmk[RTL_SAE_PMK_LEN],
                        uint8_t pmkid[RTL_SAE_PMKID_LEN])
{
    if (!sae || !pmk || !pmkid || sae->state != RTL_SAE_PEER_COMMITTED ||
        !sae->confirm_sent || !sae->peer_confirm_seen) {
        if (pmk)
            sae_secure_zero(pmk, RTL_SAE_PMK_LEN);
        if (pmkid)
            sae_secure_zero(pmkid, RTL_SAE_PMKID_LEN);
        return -1;
    }
    for (size_t i = 0; i < RTL_SAE_PMK_LEN; i++)
        pmk[i] = sae->pmk[i];
    for (size_t i = 0; i < RTL_SAE_PMKID_LEN; i++)
        pmkid[i] = sae->pmkid[i];
    rtl_sae_p256_clear(sae);
    return 0;
}

void rtl_sae_p256_clear(struct rtl_sae_p256 *sae)
{
    if (sae)
        sae_secure_zero(sae, sizeof(*sae));
}
