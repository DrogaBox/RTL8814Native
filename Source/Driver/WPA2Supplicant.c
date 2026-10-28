/*
 * RTL8814Native — In-Kernel WPA2-PSK 4-Way EAPOL Supplicant
 *
 * Self-contained IEEE 802.11i WPA2-PSK supplicant for XNU.
 * Because RTL8814Native publishes an IOEthernetInterface (en2), macOS
 * airportd does not run a WPA supplicant on en2. This module handles
 * RSN IE negotiation, PBKDF2-SHA1 PMK derivation, PRF-384 PTK derivation,
 * RFC 3394 AES-128 Key Unwrap, EAPOL-Key 4-Way Handshake, and hardware
 * CAM key installation directly inside the kext.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include "WPA2Supplicant.h"
#include "VendorGlue.hpp"
#include <linux/random.h>
#include "SAE.h"
#include "PMF.h"
#include "SecurityIE.h"
#include "ReplayCheck.h"

extern netdev_tx_t _rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev);

static void wpa2_secure_zero(void *memory, size_t length);

/* ======================================================================
 * 1. FIPS 180-4 SHA-1 & RFC 2104 HMAC-SHA1
 * ====================================================================== */

struct sha1_ctx {
	u32 state[5];
	u64 count;
	u8  buffer[64];
};

static inline u32 rol32_sha1(u32 val, int bits)
{
	return (val << bits) | (val >> (32 - bits));
}

static void sha1_transform(u32 state[5], const u8 buffer[64])
{
	u32 w[80];
	u32 a, b, c, d, e;

	for (int i = 0; i < 16; i++) {
		w[i] = ((u32)buffer[i * 4 + 0] << 24) |
		       ((u32)buffer[i * 4 + 1] << 16) |
		       ((u32)buffer[i * 4 + 2] << 8)  |
		       ((u32)buffer[i * 4 + 3]);
	}
	for (int i = 16; i < 80; i++)
		w[i] = rol32_sha1(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	for (int i = 0; i < 80; i++) {
		u32 f, k;
		if (i < 20) {
			f = (b & c) | ((~b) & d);
			k = 0x5A827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}
		u32 temp = rol32_sha1(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol32_sha1(b, 30);
		b = a;
		a = temp;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

static void sha1_init(struct sha1_ctx *ctx)
{
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xEFCDAB89;
	ctx->state[2] = 0x98BADCFE;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xC3D2E1F0;
	ctx->count = 0;
}

static void sha1_update(struct sha1_ctx *ctx, const u8 *data, size_t len)
{
	size_t idx = (size_t)(ctx->count & 0x3F);
	ctx->count += len;

	if (idx) {
		size_t left = 64 - idx;
		if (len < left) {
			_rtw_memcpy(ctx->buffer + idx, (void *)data, (u32)len);
			return;
		}
		_rtw_memcpy(ctx->buffer + idx, (void *)data, (u32)left);
		sha1_transform(ctx->state, ctx->buffer);
		data += left;
		len -= left;
	}

	while (len >= 64) {
		sha1_transform(ctx->state, data);
		data += 64;
		len -= 64;
	}

	if (len > 0)
		_rtw_memcpy(ctx->buffer, (void *)data, (u32)len);
}

static void sha1_final(struct sha1_ctx *ctx, u8 digest[20])
{
	u64 bits = ctx->count * 8;
	u8 pad = 0x80;
	u8 zero = 0x00;
	u8 len_be[8];

	for (int i = 0; i < 8; i++)
		len_be[7 - i] = (u8)(bits >> (i * 8));

	sha1_update(ctx, &pad, 1);
	while ((ctx->count & 0x3F) != 56)
		sha1_update(ctx, &zero, 1);
	sha1_update(ctx, len_be, 8);

	for (int i = 0; i < 5; i++) {
		digest[i * 4 + 0] = (u8)(ctx->state[i] >> 24);
		digest[i * 4 + 1] = (u8)(ctx->state[i] >> 16);
		digest[i * 4 + 2] = (u8)(ctx->state[i] >> 8);
		digest[i * 4 + 3] = (u8)(ctx->state[i]);
	}
	wpa2_secure_zero(len_be, sizeof(len_be));
}

static void hmac_sha1(const u8 *key, size_t key_len,
                      const u8 *data, size_t data_len, u8 mac[20])
{
	struct sha1_ctx ctx;
	u8 k_ipad[64];
	u8 k_opad[64];
	u8 tk[20];
	u8 inner[20];

	_rtw_memset(tk, 0, sizeof(tk));
	if (key_len > 64) {
		sha1_init(&ctx);
		sha1_update(&ctx, key, key_len);
		sha1_final(&ctx, tk);
		wpa2_secure_zero(&ctx, sizeof(ctx));
		key = tk;
		key_len = 20;
	}

	_rtw_memset(k_ipad, 0x36, 64);
	_rtw_memset(k_opad, 0x5c, 64);
	for (size_t i = 0; i < key_len; i++) {
		k_ipad[i] ^= key[i];
		k_opad[i] ^= key[i];
	}

	sha1_init(&ctx);
	sha1_update(&ctx, k_ipad, 64);
	sha1_update(&ctx, data, data_len);
	sha1_final(&ctx, inner);

	sha1_init(&ctx);
	sha1_update(&ctx, k_opad, 64);
	sha1_update(&ctx, inner, 20);
	sha1_final(&ctx, mac);
	wpa2_secure_zero(&ctx, sizeof(ctx));
	wpa2_secure_zero(k_ipad, sizeof(k_ipad));
	wpa2_secure_zero(k_opad, sizeof(k_opad));
	wpa2_secure_zero(tk, sizeof(tk));
	wpa2_secure_zero(inner, sizeof(inner));
}

/* ======================================================================
 * 2. PBKDF2-SHA1 (Passphrase + SSID -> 32-byte PMK) & PRF-384 (PTK)
 * ====================================================================== */

static void pbkdf2_sha1(const char *passphrase, size_t pass_len,
                        const u8 *ssid, size_t ssid_len,
                        u8 pmk[32])
{
	u8 salt_blk[40];
	u8 u[20], t[20];

	if (ssid_len > 32)
		ssid_len = 32;

	for (u32 blk = 1; blk <= 2; blk++) {
		_rtw_memcpy(salt_blk, (void *)ssid, (u32)ssid_len);
		salt_blk[ssid_len + 0] = 0;
		salt_blk[ssid_len + 1] = 0;
		salt_blk[ssid_len + 2] = 0;
		salt_blk[ssid_len + 3] = (u8)blk;

		hmac_sha1((const u8 *)passphrase, pass_len, salt_blk, ssid_len + 4, u);
		_rtw_memcpy(t, u, 20);

		for (int iter = 1; iter < 4096; iter++) {
			hmac_sha1((const u8 *)passphrase, pass_len, u, 20, u);
			for (int j = 0; j < 20; j++)
				t[j] ^= u[j];
		}

		if (blk == 1)
			_rtw_memcpy(pmk, t, 20);
		else
			_rtw_memcpy(pmk + 20, t, 12);
	}
}

static void wpa2_prf_ptk(const u8 pmk[32], const u8 aa[6], const u8 spa[6],
                         const u8 anonce[32], const u8 snonce[32], u8 ptk[48])
{
	static const char label[] = "Pairwise key expansion"; /* 22 chars + 0x00 */
	u8 buf[23 + 76 + 1];
	u8 hash[20];

	_rtw_memcpy(buf, (void *)label, 23); /* includes trailing '\0' separator */

	if (memcmp(aa, spa, 6) < 0) {
		_rtw_memcpy(buf + 23, (void *)aa, 6);
		_rtw_memcpy(buf + 29, (void *)spa, 6);
	} else {
		_rtw_memcpy(buf + 23, (void *)spa, 6);
		_rtw_memcpy(buf + 29, (void *)aa, 6);
	}

	if (memcmp(anonce, snonce, 32) < 0) {
		_rtw_memcpy(buf + 35, (void *)anonce, 32);
		_rtw_memcpy(buf + 67, (void *)snonce, 32);
	} else {
		_rtw_memcpy(buf + 35, (void *)snonce, 32);
		_rtw_memcpy(buf + 67, (void *)anonce, 32);
	}

	for (u8 i = 0; i < 3; i++) {
		buf[99] = i;
		hmac_sha1(pmk, 32, buf, 100, hash);
		if (i < 2)
			_rtw_memcpy(ptk + i * 20, hash, 20);
		else
			_rtw_memcpy(ptk + 40, hash, 8);
	}
	wpa2_secure_zero(buf, sizeof(buf));
	wpa2_secure_zero(hash, sizeof(hash));
}

/* ======================================================================
 * 3. FIPS 197 AES-128 Block Decrypt & RFC 3394 AES Key Unwrap
 * ====================================================================== */

static const u8 aes_sbox[256] = {
	0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
	0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
	0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
	0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
	0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
	0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
	0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
	0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
	0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
	0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
	0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
	0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
	0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
	0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
	0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
	0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u8 aes_inv_sbox[256] = {
	0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
	0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
	0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
	0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
	0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
	0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
	0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
	0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
	0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
	0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
	0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
	0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
	0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
	0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
	0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
	0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static const u8 aes_rcon[10] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static void wpa2_secure_zero(void *memory, size_t length)
{
	volatile u8 *p = (volatile u8 *)memory;
	while (length--)
		*p++ = 0;
}

static void aes128_expand_key(const u8 key[16], u8 rk[176])
{
	_rtw_memcpy(rk, (void *)key, 16);
	for (int i = 4; i < 44; i++) {
		u8 temp[4];
		for (int j = 0; j < 4; j++)
			temp[j] = rk[(i - 1) * 4 + j];
		if ((i & 3) == 0) {
			u8 t = temp[0];
			temp[0] = aes_sbox[temp[1]] ^ aes_rcon[(i / 4) - 1];
			temp[1] = aes_sbox[temp[2]];
			temp[2] = aes_sbox[temp[3]];
			temp[3] = aes_sbox[t];
		}
		for (int j = 0; j < 4; j++)
			rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ temp[j];
		wpa2_secure_zero(temp, sizeof(temp));
	}
}

static inline u8 gf_mul(u8 a, u8 b)
{
	u8 p = 0;
	for (int i = 0; i < 8; i++) {
		if (b & 1)
			p ^= a;
		u8 hi = a & 0x80;
		a <<= 1;
		if (hi)
			a ^= 0x1b;
		b >>= 1;
	}
	return p;
}

static void aes128_decrypt_block(const u8 rk[176], const u8 in[16], u8 out[16])
{
	u8 s[16], t[16];
	_rtw_memcpy(s, (void *)in, 16);

	/* AddRoundKey(10) */
	for (int i = 0; i < 16; i++)
		s[i] ^= rk[160 + i];

	for (int round = 9; round >= 1; round--) {
		/* InvShiftRows */
		t[0]  = s[0];  t[1]  = s[13]; t[2]  = s[10]; t[3]  = s[7];
		t[4]  = s[4];  t[5]  = s[1];  t[6]  = s[14]; t[7]  = s[11];
		t[8]  = s[8];  t[9]  = s[5];  t[10] = s[2];  t[11] = s[15];
		t[12] = s[12]; t[13] = s[9];  t[14] = s[6];  t[15] = s[3];

		/* InvSubBytes & AddRoundKey */
		for (int i = 0; i < 16; i++)
			t[i] = aes_inv_sbox[t[i]] ^ rk[round * 16 + i];

		/* InvMixColumns */
		for (int c = 0; c < 4; c++) {
			u8 s0 = t[c * 4 + 0];
			u8 s1 = t[c * 4 + 1];
			u8 s2 = t[c * 4 + 2];
			u8 s3 = t[c * 4 + 3];
			s[c * 4 + 0] = gf_mul(s0, 0x0e) ^ gf_mul(s1, 0x0b) ^ gf_mul(s2, 0x0d) ^ gf_mul(s3, 0x09);
			s[c * 4 + 1] = gf_mul(s0, 0x09) ^ gf_mul(s1, 0x0e) ^ gf_mul(s2, 0x0b) ^ gf_mul(s3, 0x0d);
			s[c * 4 + 2] = gf_mul(s0, 0x0d) ^ gf_mul(s1, 0x09) ^ gf_mul(s2, 0x0e) ^ gf_mul(s3, 0x0b);
			s[c * 4 + 3] = gf_mul(s0, 0x0b) ^ gf_mul(s1, 0x0d) ^ gf_mul(s2, 0x09) ^ gf_mul(s3, 0x0e);
		}
	}

	/* Final Round: InvShiftRows + InvSubBytes + AddRoundKey(0) */
	t[0]  = s[0];  t[1]  = s[13]; t[2]  = s[10]; t[3]  = s[7];
	t[4]  = s[4];  t[5]  = s[1];  t[6]  = s[14]; t[7]  = s[11];
	t[8]  = s[8];  t[9]  = s[5];  t[10] = s[2];  t[11] = s[15];
	t[12] = s[12]; t[13] = s[9];  t[14] = s[6];  t[15] = s[3];

	for (int i = 0; i < 16; i++)
		out[i] = aes_inv_sbox[t[i]] ^ rk[i];

	wpa2_secure_zero(s, sizeof(s));
	wpa2_secure_zero(t, sizeof(t));

}

/* RFC 3394 AES-128 Key Unwrap: cipher is (n + 1) * 8 bytes, plain is n * 8 bytes */
static int wpa2_equal(const u8 *a, const u8 *b, size_t length)
{
	u8 diff = 0;
	size_t i;
	for (i = 0; i < length; i++)
		diff = (u8)(diff | (a[i] ^ b[i]));
	return diff == 0;
}

static int aes128_unwrap(const u8 kek[16], int n, const u8 *cipher, u8 *plain)
{
	u8 rk[176] = {0};
	u8 a[8] = {0}, b[16] = {0};
	int valid = 1;

	if (n < 1 || !kek || !cipher || !plain)
		return -1;

	aes128_expand_key(kek, rk);
	_rtw_memcpy(a, (void *)cipher, 8);
	_rtw_memcpy(plain, (void *)(cipher + 8), n * 8);

	for (int j = 5; j >= 0; j--) {
		for (int i = n; i >= 1; i--) {
			u32 t = (u32)(n * j + i);
			_rtw_memcpy(b, a, 8);
			b[7] ^= (u8)(t & 0xff);
			b[6] ^= (u8)((t >> 8) & 0xff);
			b[5] ^= (u8)((t >> 16) & 0xff);
			b[4] ^= (u8)((t >> 24) & 0xff);
			_rtw_memcpy(b + 8, plain + (i - 1) * 8, 8);
			aes128_decrypt_block(rk, b, b);
			_rtw_memcpy(a, b, 8);
			_rtw_memcpy(plain + (i - 1) * 8, b + 8, 8);
		}
	}

	for (int i = 0; i < 8; i++)
		valid &= (a[i] == 0xA6);
	wpa2_secure_zero(rk, sizeof(rk));
	wpa2_secure_zero(a, sizeof(a));
	wpa2_secure_zero(b, sizeof(b));
	return valid ? 0 : -1;
}

/* ======================================================================
 * 4. In-Kernel WPA2-PSK Supplicant State Machine
 * ====================================================================== */

static struct {
	int  active;
	int  state;
	u8   pmk[32];
	u8   snonce[32];
	u8   anonce[32];
	u8   ptk[48]; /* KCK[0..15], KEK[16..31], TK[32..47] */
	u8   rsn_ie[64];
	u32  rsn_ie_len;
	u8   grp_cipher; /* 0x04 = CCMP, 0x02 = TKIP */
	int  akm_sae;    /* AKM 00-0F-AC:8: KDF-SHA256 PTK, AES-128-CMAC MIC */
	u8   igtk[16];   /* BIP-CMAC-128 key from the IGTK KDE */
	u16  igtk_id;
	int  igtk_valid;
	u64  igtk_ipn;   /* highest group management IPN accepted */
	u64  mgmt_pn;    /* highest unicast management PN accepted */
	struct rtl_replay_state replay;
	int  replay_last_message; /* 1=M1 base, 3=M3 accepted, 4=Group M1 accepted */
} g_wpa2 = {0};

/*
 * Key kept for our own protected management frames (the leave deauth). The
 * supplicant state is wiped as soon as the radio is told to leave, but the
 * deauth goes out later from the command thread, so it needs its own copy.
 */
static struct {
	int  valid;
	u8   tk[16];
	u8   bssid[6];
	u64  pn;
} g_pmf_tx = {0};

/* Take a validated IGTK KDE out of decrypted key data. */
static void wpa2_take_igtk(const struct rtl_security_key_data *parsed)
{
	if (!parsed->has_igtk)
		return;
	g_wpa2.igtk_id = parsed->igtk_id;
	_rtw_memcpy(g_wpa2.igtk, parsed->igtk, sizeof(g_wpa2.igtk));
	g_wpa2.igtk_ipn = parsed->igtk_ipn;
	g_wpa2.igtk_valid = 1;
	rtl8814native_jlog(JL_PMF, RTL_PMF_IGTK, g_wpa2.igtk_id);
}

/*
 * 802.11w receive filter for management frames, called before dispatch. Only
 * acts on an SAE link whose handshake finished (PMF is mandatory there). Returns
 * 1 to keep the frame (unicast protected frames are decrypted in place and
 * *len shrinks) and 0 to drop it.
 */
int rtl8814_wpa2_pmf_rx(u8 *frame, u32 *len)
{
	u32 n = *len;
	u8 subtype, mc;
	u16 reason = 0;

	if (!g_wpa2.akm_sae || g_wpa2.state != WPA2_STATE_COMPLETED || n < RTL_PMF_MGMT_HDR_LEN)
		return 1;
	if (((frame[0] >> 2) & 3) != 0)
		return 1;
	subtype = (u8)(frame[0] >> 4);
	if (subtype != 10 && subtype != 12 && subtype != 13) /* disassoc, deauth, action */
		return 1;
	mc = frame[4] & 1;

	if (!mc) {
		if (frame[1] & 0x40) {
			size_t out_len = 0;
			uint64_t pn = 0;

			if (rtl_pmf_ccmp_decrypt_mgmt(&g_wpa2.ptk[32], frame, n, &out_len, &pn) != 0) {
				rtl8814native_jlog(JL_PMF, RTL_PMF_DEC_FAIL, subtype);
				return 0;
			}
			if (pn <= g_wpa2.mgmt_pn) {
				rtl8814native_jlog(JL_PMF, RTL_PMF_REPLAY, subtype);
				return 0;
			}
			g_wpa2.mgmt_pn = pn;
			if (out_len >= RTL_PMF_MGMT_HDR_LEN + 2)
				reason = (u16)(frame[RTL_PMF_MGMT_HDR_LEN] | (frame[RTL_PMF_MGMT_HDR_LEN + 1] << 8));
			rtl8814native_jlog(JL_PMF, RTL_PMF_DEC_OK, subtype | (reason << 8));
			*len = (u32)out_len;
			return 1;
		}
		if (subtype != 13) {
			if (n >= RTL_PMF_MGMT_HDR_LEN + 2)
				reason = (u16)(frame[RTL_PMF_MGMT_HDR_LEN] | (frame[RTL_PMF_MGMT_HDR_LEN + 1] << 8));
			rtl8814native_jlog(JL_PMF, RTL_PMF_UNPROT_DROP, subtype | (reason << 8));
			return 0;
		}
		return 1;
	}

	/* Group addressed: BIP-CMAC-128 with the IGTK. */
	{
		unsigned int kid = 0;
		uint64_t ipn = 0;
		int has_mme = n >= RTL_PMF_MGMT_HDR_LEN + RTL_PMF_MME_LEN &&
			      frame[n - RTL_PMF_MME_LEN] == 76;

		if (subtype == 13 && (!has_mme || !g_wpa2.igtk_valid))
			return 1; /* not provably protected: leave group actions alone */
		if (!g_wpa2.igtk_valid || !has_mme ||
		    rtl_pmf_bip_verify(g_wpa2.igtk, frame, n, &kid, &ipn) != 0 || kid != g_wpa2.igtk_id) {
			rtl8814native_jlog(JL_PMF, RTL_PMF_BIP_FAIL, subtype);
			return 0;
		}
		if (ipn <= g_wpa2.igtk_ipn) {
			rtl8814native_jlog(JL_PMF, RTL_PMF_REPLAY, subtype);
			return 0;
		}
		g_wpa2.igtk_ipn = ipn;
		rtl8814native_jlog(JL_PMF, RTL_PMF_BIP_OK, subtype);
	}
	return 1;
}

/*
 * Protect our own unicast deauth, disassoc and robust action frames to the AP of
 * the last finished SAE link, in place (16 bytes spare after the frame). A PMF AP
 * ignores them otherwise: it keeps the association after a leave and then answers
 * the next join with status 30, and it never sees our BlockAck responses.
 * Returns 1 when the frame was protected.
 */
static int g_pmf_tx_enabled = 1;

void rtl8814_wpa2_pmf_tx_enable(int on)
{
	g_pmf_tx_enabled = on;
}

int rtl8814_wpa2_pmf_protect_tx(u8 *frame, u32 *len)
{
	size_t out_len = 0;
	u8 subtype, cat;

	if (!g_pmf_tx_enabled || !g_pmf_tx.valid || *len < RTL_PMF_MGMT_HDR_LEN || ((frame[0] >> 2) & 3) != 0)
		return 0;
	subtype = (u8)(frame[0] >> 4);
	if ((subtype != 10 && subtype != 12 && subtype != 13) || frame[1] & 0x40)
		return 0;
	if (_rtw_memcmp(&frame[4], g_pmf_tx.bssid, 6) != _TRUE)
		return 0;
	if (subtype == 13) {
		/* only robust action categories are protected: spectrum, QoS, DLS, Block Ack,
		 * radio measurement, fast BSS transition, SA Query, protected dual, WNM, TDLS, mesh */
		static const u8 robust[] = { 0, 1, 2, 3, 5, 6, 8, 9, 10, 12, 13, 14 };
		u32 i;
		u8 ok = 0;

		if (*len < RTL_PMF_MGMT_HDR_LEN + 1)
			return 0;
		for (i = 0; i < sizeof(robust); i++)
			if (frame[RTL_PMF_MGMT_HDR_LEN] == robust[i])
				ok = 1;
		if (!ok)
			return 0;
	}
	cat = subtype == 13 ? frame[RTL_PMF_MGMT_HDR_LEN] : 0;
	g_pmf_tx.pn++;
	if (rtl_pmf_ccmp_encrypt_mgmt(g_pmf_tx.tk, frame, *len, g_pmf_tx.pn, &out_len) != 0)
		return 0;
	*len = (u32)out_len;
	rtl8814native_jlog(JL_PMF, RTL_PMF_TX_PROTECTED, subtype | (cat << 8));
	return 1;
}

/* EAPOL-Key MIC: HMAC-SHA1 for PSK, AES-128-CMAC for SAE (16 bytes either way) */
static void wpa2_calc_mic(const u8 *data, u32 len, u8 mic[20])
{
	if (g_wpa2.akm_sae)
		rtl_aes128_cmac(g_wpa2.ptk, data, len, mic);
	else
		hmac_sha1(g_wpa2.ptk, 16, data, len, mic);
}

static int hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Look up target SSID in scanned_queue to match AP's RSN group cipher if present */
static u8 detect_ap_rsn_group_cipher(_adapter *padapter, const char *ssid, u32 ssid_len)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	_queue *queue = &pmlmepriv->scanned_queue;
	_list *head, *plist;
	_irqL irqL;
	u8 grp = 0x04; /* Default: AES-CCMP (00-0F-AC:04) */

	_enter_critical_bh(&queue->lock, &irqL);
	head = get_list_head(queue);
	plist = get_next(head);
	while (!rtw_end_of_queue_search(head, plist)) {
		struct wlan_network *pnet = LIST_CONTAINOR(plist, struct wlan_network, list);
		plist = get_next(plist);
		if (pnet->network.Ssid.SsidLength == ssid_len &&
		    _rtw_memcmp(pnet->network.Ssid.Ssid, ssid, ssid_len) == _TRUE) {
			u32 ies_len = pnet->network.IELength;
			if (ies_len > 12) {
				u8 ap_group = 0x04;
				int pmf_required = 0;
				if (rtl_security_parse_rsn(pnet->network.IEs + 12, ies_len - 12,
							   &ap_group, &pmf_required) == 0) {
					grp = ap_group;
					break;
				}
			}
		}
	}
	_exit_critical_bh(&queue->lock, &irqL);
	return grp;
}

void rtl8814_wpa2_reset(_adapter *padapter)
{
	(void)padapter;
	_rtw_memset(&g_wpa2, 0, sizeof(g_wpa2));
	g_wpa2.state = WPA2_STATE_IDLE;
}

int rtl8814_wpa2_get_state(void)
{
	return g_wpa2.state;
}

void rtl8814_wpa2_configure(_adapter *padapter, const char *ssid, u32 ssid_len,
                            const char *psk, u32 psk_len, u32 auth_mode)
{
	_rtw_memset(&g_pmf_tx, 0, sizeof(g_pmf_tx)); /* a join that is not SAE: no protected leave */
	if (!padapter)
		return;

	struct security_priv *psec = &padapter->securitypriv;
	rtl8814_wpa2_reset(padapter);

	if (auth_mode == RTL8814_AUTH_SAE) {
		rtw_reset_securitypriv(padapter);
		rtl_printk("RTL8814Native: [SAE] passphrase received (length %u); exchange not implemented\n",
			   psk_len);
		return;
	}

	if (auth_mode == 0 || psk_len == 0) {
		/* Open network */
		rtw_reset_securitypriv(padapter);
		rtl_printk("RTL8814Native: [WPA2] Configured Open network for SSID '%s'\n", ssid);
		return;
	}

	/* Compute 32-byte PMK from passphrase + SSID (or decode 64-hex PMK) */
	bool is_hex64 = (psk_len == 64);
	if (is_hex64) {
		for (int i = 0; i < 64; i++) {
			if (hex_val(psk[i]) < 0) {
				is_hex64 = false;
				break;
			}
		}
	}

	if (is_hex64) {
		for (int i = 0; i < 32; i++)
			g_wpa2.pmk[i] = (u8)((hex_val(psk[i * 2]) << 4) | hex_val(psk[i * 2 + 1]));
	} else {
		pbkdf2_sha1(psk, psk_len, (const u8 *)ssid, ssid_len, g_wpa2.pmk);
	}

	rtl_get_random_bytes(g_wpa2.snonce, 32);
	g_wpa2.grp_cipher = detect_ap_rsn_group_cipher(padapter, ssid, ssid_len);

	/* Build standard 22-byte RSN IE (Element ID 48, Length 20) */
	u8 rsn_ie[22] = {
		0x30, 0x14,                     /* Element ID: RSN (48), Length: 20 */
		0x01, 0x00,                     /* Version: 1 */
		0x00, 0x0f, 0xac, g_wpa2.grp_cipher, /* Group Data Cipher Suite */
		0x01, 0x00,                     /* Pairwise Cipher Suite Count: 1 */
		0x00, 0x0f, 0xac, 0x04,         /* Pairwise Cipher Suite: CCMP (04) */
		0x01, 0x00,                     /* AKM Suite Count: 1 */
		0x00, 0x0f, 0xac, 0x02,         /* AKM Suite: PSK (02) */
		0x00, 0x00                      /* RSN Capabilities: 0x0000 */
	};

	_rtw_memcpy(g_wpa2.rsn_ie, rsn_ie, sizeof(rsn_ie));
	g_wpa2.rsn_ie_len = sizeof(rsn_ie);
	g_wpa2.active = 1;
	g_wpa2.state = WPA2_STATE_WAIT_MSG1;

	/* Populate securitypriv->supplicant_ie (starts with Element ID 0x30, Len 0x14) */
	_rtw_memset(psec->supplicant_ie, 0, sizeof(psec->supplicant_ie));
	_rtw_memcpy(psec->supplicant_ie, rsn_ie, sizeof(rsn_ie));
	psec->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
	psec->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	psec->dot11PrivacyAlgrthm = _AES_;
	psec->dot118021XGrpPrivacy = (g_wpa2.grp_cipher == 0x02) ? _TKIP_ : _AES_;
	psec->ndisencryptstatus = Ndis802_11Encryption3Enabled;
	psec->binstallGrpkey = _FAIL;
	psec->busetkipkey = _FAIL;

	rtl_printk("RTL8814Native: [WPA2] PMK derived & RSN IE configured for SSID '%s' (grp_cipher=0x%02x)\n",
		   ssid, g_wpa2.grp_cipher);
}

/* An SAE exchange has produced a PMK: arm the handshake for AKM 8. The RSN IE
 * is the one sent in the association request (the PMKID must match). */
void rtl8814_wpa2_start_sae(_adapter *padapter, const u8 pmk[32],
                            const u8 *rsn_ie, u32 rsn_len, u8 grp_cipher)
{
	if (!padapter || !pmk || !rsn_ie || !rsn_len || rsn_len > sizeof(g_wpa2.rsn_ie))
		return;
	rtl8814_wpa2_reset(padapter);
	_rtw_memcpy(g_wpa2.pmk, (void *)pmk, 32);
	_rtw_memcpy(g_wpa2.rsn_ie, (void *)rsn_ie, rsn_len);
	g_wpa2.rsn_ie_len = rsn_len;
	g_wpa2.grp_cipher = grp_cipher;
	g_wpa2.akm_sae = 1;
	_rtw_memset(&g_pmf_tx, 0, sizeof(g_pmf_tx));
	g_wpa2.active = 1;
	g_wpa2.state = WPA2_STATE_WAIT_MSG1;
	rtl8814native_jlog(JL_WPA2, WPA2_STATE_WAIT_MSG1, 1);
}

static void wpa2_send_eapol_key(_adapter *padapter, const u8 *bssid,
                                u8 desc_type, u16 key_info,
                                const u8 replay_counter[8],
                                const u8 *nonce,
                                const u8 *key_data, u16 key_data_len)
{
	u32 body_len = 95 + key_data_len;
	u32 total_len = 14 + 4 + body_len;

	struct sk_buff *skb = _rtw_skb_alloc(total_len + 32);
	if (!skb)
		return;

	u8 *pkt = skb_put(skb, total_len);
	_rtw_memset(pkt, 0, total_len);

	/* 1. Ethernet Header (14 bytes) */
	_rtw_memcpy(pkt + 0, (void *)bssid, 6);
	_rtw_memcpy(pkt + 6, adapter_mac_addr(padapter), 6);
	pkt[12] = 0x88;
	pkt[13] = 0x8E;

	/* 2. 802.1X Header (4 bytes) */
	u8 *eapol = pkt + 14;
	eapol[0] = 0x01; /* Protocol Version: 1 (802.1X-2001, standard for WPA2) */
	eapol[1] = 0x03; /* Packet Type: EAPOL-Key (3) */
	eapol[2] = (u8)((body_len >> 8) & 0xFF);
	eapol[3] = (u8)(body_len & 0xFF);

	/* 3. EAPOL-Key Body (95 + key_data_len bytes) */
	u8 *key = eapol + 4;
	key[0] = desc_type; /* 2 = EAPOL_KEY_TYPE_RSN */
	key[1] = (u8)((key_info >> 8) & 0xFF);
	key[2] = (u8)(key_info & 0xFF);
	key[3] = 0; /* Key Length: 0 in Supplicant responses */
	key[4] = 0;
	_rtw_memcpy(key + 5, (void *)replay_counter, 8);
	if (nonce)
		_rtw_memcpy(key + 13, (void *)nonce, 32);

	key[93] = (u8)((key_data_len >> 8) & 0xFF);
	key[94] = (u8)(key_data_len & 0xFF);
	if (key_data && key_data_len > 0)
		_rtw_memcpy(key + 95, (void *)key_data, key_data_len);

	/* Compute 16-byte MIC using KCK (g_wpa2.ptk[0..15]) */
	u8 mic[20];
	wpa2_calc_mic(eapol, 4 + body_len, mic);
	_rtw_memcpy(key + 77, mic, 16);

	skb->dev = padapter->pnetdev;
	_rtw_xmit_entry(skb, padapter->pnetdev);
}

static void wpa2_install_keys(_adapter *padapter, const u8 *bssid,
                              const u8 *gtk, u32 gtk_len, u8 gtk_key_id,
                              bool install_ptk)
{
	struct security_priv *psec = &padapter->securitypriv;
	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, (u8 *)bssid);

	if (install_ptk && psta) {
		_rtw_memcpy(&psta->dot118021x_UncstKey.skey[0], &g_wpa2.ptk[32], 16);
		psta->dot118021XPrivacy = psec->dot11PrivacyAlgrthm;
		psta->ieee8021x_blocked = _FALSE;
		psta->bpairwise_key_installed = _TRUE;
		_rtw_memset(&psta->dot11txpn, 0, sizeof(union pn48));
		_rtw_memset(&psta->dot11rxpn, 0, sizeof(union pn48));
		rtw_setstakey_cmd(padapter, psta, UNICAST_KEY, _TRUE);
		rtl_printk("RTL8814Native: [WPA2] Installed Pairwise Key (TK) into hardware CAM\n");
	}

	if (gtk && gtk_len >= 16 && gtk_key_id < 4) {
		psec->dot118021XGrpKeyid = gtk_key_id;
		_rtw_memcpy(psec->dot118021XGrpKey[gtk_key_id].skey, (void *)gtk, 16);
		if (gtk_len >= 32) {
			/* TKIP group key: bytes 16..23 TX MIC, bytes 24..31 RX MIC (for AP->STA, swap rx/tx) */
			_rtw_memcpy(psec->dot118021XGrptxmickey[gtk_key_id].skey, (void *)(gtk + 16), 8);
			_rtw_memcpy(psec->dot118021XGrprxmickey[gtk_key_id].skey, (void *)(gtk + 24), 8);
		}
		psec->binstallGrpkey = _TRUE;
		psec->busetkipkey = _TRUE;

		struct sta_info *pbcmc = rtw_get_bcmc_stainfo(padapter);
		if (pbcmc) {
			pbcmc->ieee8021x_blocked = _FALSE;
			pbcmc->dot118021XPrivacy = psec->dot118021XGrpPrivacy;
			_rtw_memset(&pbcmc->dot11rxpn, 0, sizeof(union pn48));
		}

		rtw_set_key(padapter, psec, gtk_key_id, 1, _TRUE);
		rtl_printk("RTL8814Native: [WPA2] Installed Group Key (GTK idx=%u, len=%u) into hardware CAM\n",
			   gtk_key_id, gtk_len);
	}
}

int rtl8814_wpa2_rx_eapol(_adapter *padapter, struct sk_buff *skb)
{
	if (!padapter || !skb || !skb->data || skb->len < 14)
		return 0;

	const u8 *eth = skb->data;
	u16 ethertype = ((u16)eth[12] << 8) | eth[13];
	if (ethertype != 0x888E)
		return 0; /* Not an EAPOL frame */

	if (!g_wpa2.active || skb->len < 14 + 4 + 95)
		return 1; /* Consume EAPOL frame */

	const u8 *aa = eth + 6;
	const u8 *spa = adapter_mac_addr(padapter);
	const u8 *eapol = eth + 14;
	u8 eapol_type = eapol[1];
	u16 eapol_body_len = ((u16)eapol[2] << 8) | eapol[3];

	if (eapol_type != 3 || (u32)(14 + 4 + eapol_body_len) > skb->len || eapol_body_len < 95)
		return 1;

	const u8 *key = eapol + 4;
	u8  desc_type = key[0];
	u16 key_info = ((u16)key[1] << 8) | key[2];
	const u8 *replay_counter = key + 5;
	const u8 *key_nonce = key + 13;
	const u8 *key_mic = key + 77;
	u16 key_data_len = ((u16)key[93] << 8) | key[94];
	const u8 *key_data = key + 95;

	if ((u32)(95 + key_data_len) > eapol_body_len)
		return 1;

	u16 key_ver      = key_info & 0x0007;
	bool is_pairwise = (key_info & 0x0008) != 0;
	bool is_ack      = (key_info & 0x0080) != 0;
	bool is_mic      = (key_info & 0x0100) != 0;
	bool is_secure   = (key_info & 0x0200) != 0;
	bool is_encr     = (key_info & 0x1000) != 0;

	/* --- Case 1: 4-Way Handshake Message 1/4 (Pairwise + Ack, !MIC) --- */
	if (is_pairwise && is_ack && !is_mic) {
		enum rtl_replay_result replay_result = rtl_replay_check(&g_wpa2.replay, replay_counter);
		if (g_wpa2.state != WPA2_STATE_WAIT_MSG1 &&
		    g_wpa2.state != WPA2_STATE_WAIT_MSG3 &&
		    g_wpa2.state != WPA2_STATE_COMPLETED)
			return 1;
		if (g_wpa2.replay.valid && replay_result != RTL_REPLAY_NEW) {
			int same_anonce = _rtw_memcmp(g_wpa2.anonce, key_nonce, 32) == _TRUE;
			if (replay_result == RTL_REPLAY_REPEAT &&
			    g_wpa2.replay_last_message == 1 && same_anonce &&
			    (g_wpa2.state == WPA2_STATE_WAIT_MSG3 ||
			     g_wpa2.state == WPA2_STATE_COMPLETED)) {
				u16 reply_info = key_ver | 0x0008 | 0x0100;
				rtl8814native_jlog(JL_WPA2_REPLAY, 0x101, RTL_REPLAY_REPEAT);
				wpa2_send_eapol_key(padapter, aa, desc_type, reply_info,
				                    replay_counter, g_wpa2.snonce,
				                    g_wpa2.rsn_ie, (u16)g_wpa2.rsn_ie_len);
				return 1;
			}
			if (same_anonce) {
				rtl8814native_jlog(JL_WPA2_REPLAY, 1, (int)replay_result);
				return 1;
			}
			/* A different ANonce starts a new handshake, so the M1 below
			 * replaces the saved replay-counter base even if it moved backwards. */
		}
		_rtw_memcpy(g_wpa2.anonce, (void *)key_nonce, 32);
		if (g_wpa2.akm_sae) {
			/* the SNonce protects the PTK: no xorshift here */
			if (rtl_sae_entropy_fill(NULL, g_wpa2.snonce, 32)) {
				g_wpa2.state = WPA2_STATE_FAILED;
				rtl8814native_jlog(JL_WPA2, WPA2_STATE_FAILED, 0);
				return 1;
			}
			rtl_sae_derive_ptk(g_wpa2.pmk, aa, spa, g_wpa2.anonce, g_wpa2.snonce, g_wpa2.ptk);
		} else {
			rtl_get_random_bytes(g_wpa2.snonce, 32);
			wpa2_prf_ptk(g_wpa2.pmk, aa, spa, g_wpa2.anonce, g_wpa2.snonce, g_wpa2.ptk);
		}

		u16 reply_info = key_ver | 0x0008 | 0x0100; /* Pairwise | MIC */
		wpa2_send_eapol_key(padapter, aa, desc_type, reply_info,
		                    replay_counter, g_wpa2.snonce,
		                    g_wpa2.rsn_ie, (u16)g_wpa2.rsn_ie_len);
		rtl_replay_begin(&g_wpa2.replay, replay_counter);
		g_wpa2.replay_last_message = 1;
		g_wpa2.state = WPA2_STATE_WAIT_MSG3;
		rtl8814native_jlog(JL_WPA2, WPA2_STATE_WAIT_MSG3, 0);
		rtl_printk("RTL8814Native: [WPA2] RX EAPOL Msg 1/4 -> Sent Msg 2/4 (AP %02x:%02x:%02x:%02x:%02x:%02x)\n",
			   aa[0], aa[1], aa[2], aa[3], aa[4], aa[5]);
		return 1;
	}

	/* --- Case 2: 4-Way Handshake Message 3/4 (Pairwise + Ack + MIC) --- */
	if (is_pairwise && is_ack && is_mic) {
		enum rtl_replay_result replay_result = rtl_replay_check(&g_wpa2.replay, replay_counter);
		/* An M3 after the handshake finished is the AP retrying because our M4 was lost:
		 * answer with M4 again, never reinstall keys. The retry may carry a fresh counter. */
		int duplicate_m3 = g_wpa2.state == WPA2_STATE_COMPLETED &&
		                   (replay_result == RTL_REPLAY_NEW ||
		                    (replay_result == RTL_REPLAY_REPEAT && g_wpa2.replay_last_message == 3));
		if (replay_result != RTL_REPLAY_NEW && !duplicate_m3) {
			rtl8814native_jlog(JL_WPA2_REPLAY, 3, (int)replay_result);
			return 1;
		}
		if (g_wpa2.state != WPA2_STATE_WAIT_MSG3 && !duplicate_m3)
			return 1;

		/* Verify MIC over EAPOL frame with key_mic zeroed */
		u32 tot_eapol = 4 + eapol_body_len;
		u8 *tmp = (u8 *)rtw_malloc(tot_eapol);
		if (!tmp)
			return 1;
		_rtw_memcpy(tmp, (void *)eapol, tot_eapol);
		_rtw_memset(tmp + 4 + 77, 0, 16);

		u8 calc_mic[20];
		wpa2_calc_mic(tmp, tot_eapol, calc_mic);
		wpa2_secure_zero(tmp, tot_eapol);
		rtw_mfree(tmp, tot_eapol);

		if (!wpa2_equal(calc_mic, key_mic, 16)) {
			if (duplicate_m3) {
				rtl8814native_jlog(JL_WPA2_REPLAY, 3, RTL_REPLAY_REPEAT);
				wpa2_secure_zero(calc_mic, sizeof(calc_mic));
				return 1;
			}
			rtl_printk("RTL8814Native: [WPA2] ERROR: Msg 3/4 MIC verification failed (wrong password?)\n");
			g_wpa2.state = WPA2_STATE_FAILED;
			rtl8814native_jlog(JL_WPA2, WPA2_STATE_FAILED, 0);
			wpa2_secure_zero(calc_mic, sizeof(calc_mic));
			return 1;
		}

		if (duplicate_m3) {
			u16 reply_info = key_ver | 0x0008 | 0x0100 | (is_secure ? 0x0200 : 0);
			rtl8814native_jlog(JL_WPA2_REPLAY, 0x103, (int)replay_result);
			wpa2_send_eapol_key(padapter, aa, desc_type, reply_info,
			                    replay_counter, NULL, NULL, 0);
			if (replay_result == RTL_REPLAY_NEW) {
				rtl_replay_accept(&g_wpa2.replay, replay_counter);
				g_wpa2.replay_last_message = 3;
			}
			wpa2_secure_zero(calc_mic, sizeof(calc_mic));
			return 1;
		}
		/* Unwrap Encrypted Key Data (RFC 3394 AES-128 Key Unwrap) to extract GTK */
		u8 gtk_buf[32] = {0};
		u32 gtk_len = 0;
		u8 gtk_key_id = 1;

		if (is_encr && key_data_len >= 24 && (key_data_len % 8) == 0) {
			int n = (key_data_len / 8) - 1;
			u8 *plain = (u8 *)rtw_zmalloc(n * 8);
			if (plain) {
				if (aes128_unwrap(&g_wpa2.ptk[16], n, key_data, plain) == 0) {
					int plain_len = n * 8;
					struct rtl_security_key_data parsed = {0};
					if (rtl_security_parse_key_data(plain, (size_t)plain_len, 0, &parsed) == 0) {
						if (parsed.has_gtk) {
							gtk_key_id = parsed.gtk_key_id;
							gtk_len = (u32)parsed.gtk_len;
							_rtw_memcpy(gtk_buf, parsed.gtk, gtk_len);
						}
						if (g_wpa2.akm_sae)
							wpa2_take_igtk(&parsed);
					}
					wpa2_secure_zero(&parsed, sizeof(parsed));
				} else {
					rtl_printk("RTL8814Native: [WPA2] Warning: AES Key Unwrap failed on Msg 3/4\n");
				}
				wpa2_secure_zero(plain, (size_t)n * 8U);
				rtw_mfree(plain, n * 8);
			}
		}

		/* Send Message 4/4 BEFORE enabling pairwise encryption in hardware CAM */
		u16 reply_info = key_ver | 0x0008 | 0x0100 | (is_secure ? 0x0200 : 0);
		wpa2_send_eapol_key(padapter, aa, desc_type, reply_info,
		                    replay_counter, NULL, NULL, 0);

		/* Install Pairwise Key (TK) and Group Key (GTK) and unblock 802.1X port */
		wpa2_install_keys(padapter, aa, gtk_len ? gtk_buf : NULL, gtk_len, gtk_key_id, true);
		wpa2_secure_zero(gtk_buf, sizeof(gtk_buf));
		wpa2_secure_zero(calc_mic, sizeof(calc_mic));
		rtl_replay_accept(&g_wpa2.replay, replay_counter);
		g_wpa2.replay_last_message = 3;
		g_wpa2.state = WPA2_STATE_COMPLETED;
		rtl8814native_jlog(JL_WPA2, WPA2_STATE_COMPLETED, 0);
		if (g_wpa2.akm_sae) {
			_rtw_memcpy(g_pmf_tx.tk, &g_wpa2.ptk[32], 16);
			_rtw_memcpy(g_pmf_tx.bssid, aa, 6);
			g_pmf_tx.pn = ((u64)rtw_get_current_time()) << 12;
			g_pmf_tx.valid = 1;
		}

		struct net_device *ndev = padapter->pnetdev ? padapter->pnetdev : rtl8814native_get_netdev();
		if (ndev)
			rtl_netif_carrier(ndev, 1);

		rtl_printk("RTL8814Native: [WPA2] RX EAPOL Msg 3/4 -> Sent Msg 4/4 — 4-WAY HANDSHAKE COMPLETE!\n");
		return 1;
	}

	/* --- Case 3: Group Key Handshake Message 1/2 (!Pairwise + Ack + MIC) --- */
	if (!is_pairwise && is_ack && is_mic) {
		enum rtl_replay_result replay_result = rtl_replay_check(&g_wpa2.replay, replay_counter);
		if (replay_result != RTL_REPLAY_NEW || g_wpa2.state != WPA2_STATE_COMPLETED) {
			if (replay_result != RTL_REPLAY_NEW)
				rtl8814native_jlog(JL_WPA2_REPLAY, 4, (int)replay_result);
			return 1;
		}

		u32 tot_eapol = 4 + eapol_body_len;
		u8 *tmp = (u8 *)rtw_malloc(tot_eapol);
		if (!tmp)
			return 1;
		_rtw_memcpy(tmp, (void *)eapol, tot_eapol);
		_rtw_memset(tmp + 4 + 77, 0, 16);

		u8 calc_mic[20];
		wpa2_calc_mic(tmp, tot_eapol, calc_mic);
		wpa2_secure_zero(tmp, tot_eapol);
		rtw_mfree(tmp, tot_eapol);

		if (!wpa2_equal(calc_mic, key_mic, 16)) {
			wpa2_secure_zero(calc_mic, sizeof(calc_mic));
			return 1;
		}

		u8 gtk_buf[32] = {0};
		u32 gtk_len = 0;
		u8 gtk_key_id = (u8)((key_info >> 4) & 0x03);
		if (gtk_key_id == 0)
			gtk_key_id = 1;

		if (is_encr && key_data_len >= 24 && (key_data_len % 8) == 0) {
			int n = (key_data_len / 8) - 1;
			u8 *plain = (u8 *)rtw_zmalloc(n * 8);
			if (plain) {
				if (aes128_unwrap(&g_wpa2.ptk[16], n, key_data, plain) == 0) {
					struct rtl_security_key_data parsed = {0};
					if (rtl_security_parse_key_data(plain, (size_t)(n * 8), 1, &parsed) == 0) {
						if (parsed.has_gtk) {
							gtk_key_id = parsed.gtk_key_id == 0xff ? gtk_key_id : parsed.gtk_key_id;
							gtk_len = (u32)parsed.gtk_len;
							_rtw_memcpy(gtk_buf, parsed.gtk, gtk_len);
						}
						if (g_wpa2.akm_sae)
							wpa2_take_igtk(&parsed);
					}
					wpa2_secure_zero(&parsed, sizeof(parsed));
				}
				wpa2_secure_zero(plain, (size_t)n * 8U);
				rtw_mfree(plain, n * 8);
			}
		}

		if (gtk_len >= 16)
			wpa2_install_keys(padapter, aa, gtk_buf, gtk_len, gtk_key_id, false);

		u16 reply_info = key_ver | 0x0100 | (is_secure ? 0x0200 : 0);
		wpa2_send_eapol_key(padapter, aa, desc_type, reply_info,
		                    replay_counter, NULL, NULL, 0);
		wpa2_secure_zero(gtk_buf, sizeof(gtk_buf));
		wpa2_secure_zero(calc_mic, sizeof(calc_mic));
		rtl_replay_accept(&g_wpa2.replay, replay_counter);
		g_wpa2.replay_last_message = 4;
		rtl_printk("RTL8814Native: [WPA2] Group Key Handshake Msg 1/2 -> Sent Msg 2/2\n");
		return 1;
	}

	return 1;
}
