/*
 * Offline integration test for the production WPA2 supplicant.
 * Build with: Tools/run_eapol_flow_test.sh
 */
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ReplayCheck.h"
#include "SAE.h"
#include "SecurityIE.h"
#include "RTLJoinLog.h"
#include "../Source/Driver/PMF.h"

/* Skip kernel-facing headers while compiling the production translation unit here. */
#define _RTL8814_WPA2_SUPPLICANT_H
#define _RTL8814_VENDOR_GLUE_H
#define _LINUX_RANDOM_H_RTL8814

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int netdev_tx_t;
typedef struct sk_buff _pkt;
typedef void *_nic_hdl;
typedef int bool;
#ifndef true
#define true 1
#define false 0
#endif
#define _TRUE 1
#define _FALSE 0
#define _AES_ 4
#define _TKIP_ 2
#define WPA2_STATE_IDLE 0
#define WPA2_STATE_WAIT_MSG1 1
#define WPA2_STATE_WAIT_MSG3 2
#define WPA2_STATE_COMPLETED 3
#define WPA2_STATE_FAILED 4
#define RTL8814_AUTH_SAE 2
#define RTL_PMF_MGMT_HDR_LEN 24U
#define RTL_PMF_MME_LEN 18U
#define Ndis802_11AuthModeWPA2PSK 6
#define dot11AuthAlgrthm_8021X 3
#define Ndis802_11Encryption3Enabled 4
#define _FAIL 0
#define UNICAST_KEY 0
#define JL_WPA2 1
#define JL_PMF 2
#define JL_WPA2_REPLAY 3
#define RTL_PMF_IGTK 1
#define RTL_PMF_TX_PROTECTED 8

#define _rtw_memcpy(d,s,n) memcpy((d),(s),(n))
#define _rtw_memset(d,v,n) memset((d),(v),(n))
#define _rtw_memcmp(a,b,n) (memcmp((a),(b),(n)) == 0)
#define _enter_critical_bh(lock, irq) ((void)(lock), (void)(irq))
#define _exit_critical_bh(lock, irq) ((void)(lock), (void)(irq))

struct sk_buff { u8 *data; size_t len; size_t capacity; void *dev; };
struct sk_buff *_rtw_skb_alloc(size_t size);
u8 *__skb_put(struct sk_buff *skb, size_t len);
#define skb_put(skb, len) __skb_put((skb), (len))
struct net_device { int unused; };
struct qnode { struct qnode *next; };
struct __queue { void *lock; struct qnode sentinel; };
typedef struct __queue _queue;
typedef struct qnode _list;
typedef unsigned long _irqL;
#define get_list_head(q) (&(q)->sentinel)
#define get_next(n) ((n)->next)
#define LIST_CONTAINOR(ptr,type,member) ((type *)((char *)(ptr) - offsetof(type, member)))
struct _ADAPTER;
typedef struct _ADAPTER _adapter;
struct ndis_802_11_ssid { u32 SsidLength; u8 Ssid[32]; };
struct wlan_bssid_ex { struct ndis_802_11_ssid Ssid; u32 IELength; u8 IEs[512]; };
struct wlan_network { struct wlan_bssid_ex network; struct qnode list; };
struct mlme_priv { struct __queue scanned_queue; };
union Keytype { u8 skey[32]; };
union pn48 { u8 val[8]; };
struct security_priv {
 u8 supplicant_ie[512]; u32 dot11PrivacyAlgrthm, dot118021XGrpPrivacy, dot11AuthAlgrthm;
 u32 ndisauthtype, ndisencryptstatus, binstallGrpkey, busetkipkey, dot118021XGrpKeyid;
 union Keytype dot118021XGrpKey[6], dot118021XGrptxmickey[6], dot118021XGrprxmickey[6];
};
struct sta_info {
 u8 mac[6];
 union Keytype dot118021x_UncstKey;
 u32 dot118021XPrivacy, ieee8021x_blocked, bpairwise_key_installed;
 union pn48 dot11txpn, dot11rxpn;
};
struct sta_priv { struct sta_info ap, bcmc; };
struct _ADAPTER {
 struct security_priv securitypriv;
 struct mlme_priv mlmepriv;
 struct sta_priv stapriv;
 struct net_device *pnetdev;
 u8 mac_addr[6];
};
#define adapter_mac_addr(a) ((a)->mac_addr)

static u8 captured[16][2048];
static size_t captured_len[16];
static unsigned tx_count, ptk_install_count, gtk_install_count;
static unsigned last_gtk_len, last_gtk_id;
static unsigned jlog_count[4];
static _adapter adapter;
static struct sta_info *test_ap;
static struct net_device test_netdev;

static void fail(const char *msg, int line)
{
 fprintf(stderr, "FAIL line %d: %s\n", line, msg);
 exit(1);
}
#define CHECK(c) do { if (!(c)) fail(#c, __LINE__); } while (0)

netdev_tx_t _rtw_xmit_entry(_pkt *pkt, _nic_hdl dev);
void rtl_printk(const char *fmt, ...);
struct net_device *rtl8814native_get_netdev(void);
void rtl_netif_carrier(struct net_device *dev, int on);
void rtl_get_random_bytes(void *buf, int len);
int rtl_sae_entropy_fill(void *ctx, uint8_t *out, size_t len);
void rtw_reset_securitypriv(_adapter *a);
struct sta_info *rtw_get_stainfo(void *a, u8 *bssid);
struct sta_info *rtw_get_bcmc_stainfo(_adapter *a);
int rtw_end_of_queue_search(void *head, void *item);
void rtw_setstakey_cmd(_adapter *a, struct sta_info *sta, int type, int enqueue);
int rtw_set_key(_adapter *a, struct security_priv *s, int id, u8 tx, bool enqueue);
void *rtw_malloc(size_t n);
void *rtw_zmalloc(size_t n);
void rtw_mfree(void *p, size_t n);
int rtw_get_current_time(void);

void rtl8814native_jlog(unsigned int ev, int a, int b)
{
 (void)a; (void)b;
 if (ev < 4) jlog_count[ev]++;
}
void rtl_printk(const char *fmt, ...) { (void)fmt; }
struct net_device *rtl8814native_get_netdev(void) { return &test_netdev; }
void rtl_netif_carrier(struct net_device *dev, int on) { (void)dev; (void)on; }
void rtl_get_random_bytes(void *buf, int len)
{
 memset(buf, 0x5a, (size_t)len);
}
int rtl_sae_entropy_fill(void *ctx, uint8_t *out, size_t len)
{
 (void)ctx;
 memset(out, 0x5a, len);
 return 0;
}
void rtw_reset_securitypriv(_adapter *a) { memset(&a->securitypriv, 0, sizeof(a->securitypriv)); }
struct sta_info *rtw_get_stainfo(void *a, u8 *bssid)
{
 (void)a; (void)bssid; return test_ap;
}
struct sta_info *rtw_get_bcmc_stainfo(_adapter *a) { return &a->stapriv.bcmc; }
int rtw_end_of_queue_search(void *head, void *item) { return item == NULL || item == head; }
int rtl8814native_get_netdev_dummy(void) { return 0; }
int rtw_get_current_time(void) { return 7; }
void rtw_setstakey_cmd(_adapter *a, struct sta_info *sta, int type, int enqueue)
{
 (void)a; (void)sta; (void)type; (void)enqueue; ++ptk_install_count;
}
int rtw_set_key(_adapter *a, struct security_priv *s, int id, u8 tx, bool enqueue)
{
 (void)a; (void)s; (void)tx; (void)enqueue; ++gtk_install_count;
 last_gtk_len = 16; last_gtk_id = (unsigned)id;
 return 0;
}
void rtl8814native_dummy_set_key(void) {}

struct sk_buff *_rtw_skb_alloc(size_t size)
{
 struct sk_buff *skb = calloc(1, sizeof(*skb));
 if (!skb) return NULL;
 skb->data = calloc(1, size);
 if (!skb->data) { free(skb); return NULL; }
 skb->capacity = size;
 return skb;
}
u8 *__skb_put(struct sk_buff *skb, size_t len)
{
 if (len > skb->capacity - skb->len) return NULL;
 u8 *p = skb->data + skb->len; skb->len += len; return p;
}
netdev_tx_t _rtw_xmit_entry(_pkt *skb, _nic_hdl dev)
{
 (void)dev;
 if (tx_count < 16 && skb->len <= sizeof(captured[0])) {
  memcpy(captured[tx_count], skb->data, skb->len);
  captured_len[tx_count++] = skb->len;
 }
 free(skb->data); free(skb); return 0;
}
void *rtw_malloc(size_t n) { return malloc(n); }
void *rtw_zmalloc(size_t n) { return calloc(1, n); }
void rtw_mfree(void *p, size_t n) { (void)n; free(p); }

#include "../Source/Driver/WPA2Supplicant.c"

static const u8 PMK[32] = {
 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xf0,0x01,
 0x12,0x23,0x34,0x45,0x56,0x67,0x78,0x89,0x9a,0xab,0xbc,0xcd,0xde,0xef,0xf1,0x02
};
static const u8 AP[6] = {0x02,0xaa,0xbb,0xcc,0xdd,0x01};
static const u8 STA[6] = {0x02,0x10,0x20,0x30,0x40,0x50};
static const u8 GTK[16] = {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40};
static const u8 IGTK[16] = {0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60};
static u8 snonce[32], anonce[32], ptk[48];

static void put_le16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

static size_t make_kde_data(u8 *out, int with_igtk, int malformed)
{
 size_t n = 0;
 out[n++] = 0xdd; out[n++] = 22;
 out[n++] = 0x00; out[n++] = 0x0f; out[n++] = 0xac; out[n++] = 1;
 out[n++] = 1; out[n++] = 0;
 memcpy(out + n, GTK, 16); n += 16;
 if (with_igtk) {
  out[n++] = 0xdd; out[n++] = 28;
  out[n++] = 0x00; out[n++] = 0x0f; out[n++] = 0xac; out[n++] = 9;
  put_le16(out + n, 4); n += 2;
  memset(out + n, 0, 6); n += 6;
  memcpy(out + n, IGTK, 16); n += 16;
 }
 while ((n & 7U) != 0) out[n++] = 0;
 if (malformed) {
  out[n++] = 0xdd; out[n++] = 22;
  memset(out + n, 0, 6); n += 6; /* incomplete vendor KDE, still wrap-aligned */
 }
 return n;
}

static void aes_encrypt_block(const u8 key[16], const u8 in[16], u8 out[16])
{
 u8 rk[176], s[16], t[16];
 aes128_expand_key(key, rk);
 memcpy(s, in, 16);
 for (int i = 0; i < 16; ++i) s[i] ^= rk[i];
 for (int round = 1; round <= 10; ++round) {
  for (int i = 0; i < 16; ++i) t[i] = aes_sbox[s[i]];
  s[0]=t[0]; s[1]=t[5]; s[2]=t[10]; s[3]=t[15];
  s[4]=t[4]; s[5]=t[9]; s[6]=t[14]; s[7]=t[3];
  s[8]=t[8]; s[9]=t[13]; s[10]=t[2]; s[11]=t[7];
  s[12]=t[12]; s[13]=t[1]; s[14]=t[6]; s[15]=t[11];
  if (round != 10) {
   for (int c = 0; c < 4; ++c) {
    u8 *p = s + c * 4; u8 a=p[0],b=p[1],c0=p[2],d=p[3];
    p[0]=gf_mul(a,2)^gf_mul(b,3)^c0^d;
    p[1]=a^gf_mul(b,2)^gf_mul(c0,3)^d;
    p[2]=a^b^gf_mul(c0,2)^gf_mul(d,3);
    p[3]=gf_mul(a,3)^b^c0^gf_mul(d,2);
   }
  }
  for (int i = 0; i < 16; ++i) s[i] ^= rk[round * 16 + i];
 }
 memcpy(out, s, 16); memset(rk,0,sizeof(rk)); memset(s,0,sizeof(s)); memset(t,0,sizeof(t));
}
static size_t aes_wrap(const u8 kek[16], const u8 *plain, size_t plain_len, u8 *cipher)
{
 size_t n = plain_len / 8; u8 a[8];
 memset(a, 0xa6, sizeof(a));
 memcpy(cipher + 8, plain, plain_len);
 for (unsigned j = 0; j < 6; ++j) for (size_t i = 1; i <= n; ++i) {
  u8 b[16]; memcpy(b, a, 8); memcpy(b + 8, cipher + i * 8, 8);
  aes_encrypt_block(kek, b, b);
  u64 t = (u64)n * j + i;
  for (unsigned k = 0; k < 8; ++k) a[k] = b[k] ^ (u8)(t >> (56 - 8 * k));
  memcpy(cipher + i * 8, b + 8, 8);
 }
 memcpy(cipher, a, 8); return plain_len + 8;
}

static size_t build_frame(u8 *frame, u8 msg, u64 replay, const u8 nonce[32],
                          const u8 *plain_data, size_t plain_len, int corrupt_mic,
                          int wrong_key_data_len)
{
 u8 *eapol, *key;
 u16 info = 0x0002; /* descriptor version 2 */
 size_t key_data_len = 0;
 u8 wrapped[256];
 if (msg == 1) info |= 0x0088;              /* pairwise, ack */
 else if (msg == 3) info |= 0x01c8 | 0x1000; /* pairwise, ack, mic, secure, encrypted */
 else info |= 0x0380 | 0x1000;              /* group, ack, mic, secure, encrypted */
 if (plain_data && plain_len) {
  key_data_len = aes_wrap(ptk + 16, plain_data, plain_len, wrapped);
  u8 check_plain[256];
  CHECK((key_data_len % 8) == 0 && key_data_len >= 24);
  CHECK(aes128_unwrap(ptk + 16, (int)(key_data_len / 8 - 1), wrapped, check_plain) == 0);
  CHECK(memcmp(check_plain, plain_data, plain_len) == 0);
  memset(check_plain, 0, sizeof(check_plain));
 }
 size_t body_len = 95 + key_data_len;
 size_t total = 14 + 4 + body_len;
 memset(frame, 0, total);
 memcpy(frame, STA, 6); memcpy(frame + 6, AP, 6);
 frame[12] = 0x88; frame[13] = 0x8e;
 eapol = frame + 14; eapol[0] = 2; eapol[1] = 3;
 put_be16(eapol + 2, (u16)body_len);
 key = eapol + 4; key[0] = 2;
 put_be16(key + 1, info);
 put_be16(key + 3, 16);
 for (unsigned i = 0; i < 8; ++i) key[5 + i] = (u8)(replay >> ((7 - i) * 8));
 if (nonce) memcpy(key + 13, nonce, 32);
 put_be16(key + 93, (u16)(wrong_key_data_len ? key_data_len + 17 : key_data_len));
 if (key_data_len) memcpy(key + 95, wrapped, key_data_len);
 memset(key + 77, 0, 16);
 u8 mic[16];
 rtl_aes128_cmac(ptk, eapol, 4 + body_len, mic);
 memcpy(key + 77, mic, 16);
 if (corrupt_mic) key[77] ^= 1;
 return total;
}

static void deliver(const u8 *bytes, size_t len)
{
 u8 *bounded = malloc(len ? len : 1);
 CHECK(bounded != NULL);
 if (len) memcpy(bounded, bytes, len);
 struct sk_buff skb = {bounded, len, len, NULL};
 (void)rtl8814_wpa2_rx_eapol(&adapter, &skb);
 free(bounded);
}
static void reset_case(void)
{
 memset(&adapter, 0, sizeof(adapter)); memset(&test_netdev, 0, sizeof(test_netdev));
 adapter.pnetdev = &test_netdev; memcpy(adapter.mac_addr, STA, 6);
 memcpy(adapter.stapriv.ap.mac, AP, 6); test_ap = &adapter.stapriv.ap;
 tx_count = ptk_install_count = gtk_install_count = last_gtk_len = last_gtk_id = 0;
 memset(captured_len, 0, sizeof(captured_len)); memset(jlog_count, 0, sizeof(jlog_count));
 rtl8814_wpa2_start_sae(&adapter, PMK, (const u8 *)"rsn", 3, 4);
}
static void derive(const u8 *an)
{
 memcpy(anonce, an, 32);
 memset(snonce, 0x5a, sizeof(snonce));
 rtl_sae_derive_ptk(PMK, AP, STA, anonce, snonce, ptk);
}
static void send_m1(u64 replay, const u8 *an)
{
 u8 f[256]; derive(an); size_t n = build_frame(f, 1, replay, an, NULL, 0, 0, 0); deliver(f, n);
}
static void send_m3(u64 replay, int bad_mic, int malformed)
{
 u8 plain[64], f[512]; size_t plain_len = make_kde_data(plain, 1, malformed);
 size_t n = build_frame(f, 3, replay, anonce, plain, plain_len, bad_mic, 0); deliver(f, n);
}
static void send_group(u64 replay, int bad_mic, int malformed)
{
 u8 plain[64], f[512]; size_t plain_len = make_kde_data(plain, 1, malformed);
 size_t n = build_frame(f, 4, replay, NULL, plain, plain_len, bad_mic, 0); deliver(f, n);
}

static unsigned response_key_info(unsigned index)
{
 CHECK(index < tx_count && captured_len[index] >= 14 + 4 + 95);
 return ((unsigned)captured[index][14 + 4 + 1] << 8) | captured[index][14 + 4 + 2];
}
static u64 accepted_counter(void)
{
 u64 value = 0;
 for (unsigned i = 0; i < 8; ++i) value = (value << 8) | g_wpa2.replay.last[i];
 return value;
}
static void send_m1_expect(u64 replay, const u8 *nonce)
{
 send_m1(replay, nonce);
 CHECK(tx_count > 0);
 CHECK((response_key_info(tx_count - 1) & 0x0108) == 0x0108);
 CHECK(memcmp(captured[tx_count - 1] + 14 + 4 + 13, g_wpa2.snonce, 32) == 0);
}
static void case_normal_retransmits_replays_and_group(void)
{
 static const u8 nonce_a[32] = {0x01};
 u8 saved_anonce[32];
 reset_case();
 send_m1_expect(10, nonce_a);
 CHECK(rtl8814_wpa2_get_state() == WPA2_STATE_WAIT_MSG3 && ptk_install_count == 0);
 u8 first_snonce[32]; memcpy(first_snonce, g_wpa2.snonce, 32);
 unsigned tx_after_first_m1 = tx_count;
 send_m1_expect(10, nonce_a);
 CHECK(tx_count == tx_after_first_m1 + 1);
 CHECK(rtl8814_wpa2_get_state() == WPA2_STATE_WAIT_MSG3 && ptk_install_count == 0);
 CHECK(memcmp(first_snonce, g_wpa2.snonce, 32) == 0);
 memcpy(saved_anonce, nonce_a, 32);
 memcpy(anonce, saved_anonce, 32);
 memcpy(snonce, g_wpa2.snonce, 32);
 rtl_sae_derive_ptk(PMK, AP, STA, anonce, snonce, ptk);
 send_m3(11, 0, 0);
 CHECK(tx_count == tx_after_first_m1 + 2);
 CHECK((response_key_info(tx_count - 1) & 0x0108) == 0x0108);
 CHECK(rtl8814_wpa2_get_state() == WPA2_STATE_COMPLETED);
 CHECK(ptk_install_count == 1 && gtk_install_count == 1);
 CHECK(g_wpa2.igtk_valid && g_wpa2.igtk_id == 4);
 CHECK(memcmp(g_wpa2.igtk, IGTK, 16) == 0);
 CHECK(memcmp(adapter.securitypriv.dot118021XGrpKey[1].skey, GTK, 16) == 0);

 memset(adapter.stapriv.ap.dot11txpn.val, 0x5a, sizeof(adapter.stapriv.ap.dot11txpn.val));
 unsigned install_ptk = ptk_install_count, install_gtk = gtk_install_count;
 unsigned tx_before_retry = tx_count;
 send_m3(11, 0, 0);
 CHECK(tx_count == tx_before_retry + 1 && ptk_install_count == install_ptk && gtk_install_count == install_gtk);
 CHECK(adapter.stapriv.ap.dot11txpn.val[0] == 0x5a);
 tx_before_retry = tx_count;
 send_m3(12, 0, 0);
 CHECK(tx_count == tx_before_retry + 1 && ptk_install_count == install_ptk && gtk_install_count == install_gtk);
 CHECK(adapter.stapriv.ap.dot11txpn.val[0] == 0x5a && accepted_counter() == 12);
 tx_before_retry = tx_count;
 send_m3(11, 0, 0);
 CHECK(tx_count == tx_before_retry && ptk_install_count == install_ptk && gtk_install_count == install_gtk);
 tx_before_retry = tx_count;
 send_m3(13, 1, 0);
 CHECK(tx_count == tx_before_retry && ptk_install_count == install_ptk && gtk_install_count == install_gtk);

 tx_before_retry = tx_count;
 send_group(14, 0, 0);
 CHECK(tx_count == tx_before_retry + 1 && gtk_install_count == install_gtk + 1);
 CHECK(memcmp(adapter.securitypriv.dot118021XGrpKey[1].skey, GTK, 16) == 0);
 CHECK(g_wpa2.igtk_valid && memcmp(g_wpa2.igtk, IGTK, 16) == 0);
 install_gtk = gtk_install_count;
 tx_before_retry = tx_count;
 send_group(14, 0, 0);
 CHECK(tx_count == tx_before_retry && gtk_install_count == install_gtk);
 send_group(13, 0, 0);
 CHECK(tx_count == tx_before_retry && gtk_install_count == install_gtk);
 tx_before_retry = tx_count;
 send_group(15, 0, 1);
 CHECK(gtk_install_count == install_gtk);
 CHECK(memcmp(adapter.securitypriv.dot118021XGrpKey[1].skey, GTK, 16) == 0);
 (void)tx_before_retry;
}
static void case_new_m1_lower_counter(void)
{
 static const u8 nonce_a[32] = {0x02};
 static const u8 nonce_b[32] = {0x03};
 reset_case(); send_m1_expect(500, nonce_a);
 send_m1_expect(3, nonce_b);
 CHECK(rtl8814_wpa2_get_state() == WPA2_STATE_WAIT_MSG3);
 CHECK(accepted_counter() == 3 && memcmp(g_wpa2.anonce, nonce_b, 32) == 0);
 CHECK(ptk_install_count == 0);
}
static void case_malformed_group_no_key_install(void)
{
 static const u8 nonce[32] = {0x04};
 reset_case(); send_m1_expect(20, nonce); memcpy(anonce, nonce, 32);
 memcpy(snonce, g_wpa2.snonce, 32);
 rtl_sae_derive_ptk(PMK, AP, STA, anonce, snonce, ptk); send_m3(21, 0, 0);
 CHECK(rtl8814_wpa2_get_state() == WPA2_STATE_COMPLETED);
 unsigned installs = gtk_install_count, tx_before = tx_count;
 send_group(22, 0, 1);
 CHECK(gtk_install_count == installs);
 CHECK(tx_count == tx_before + 1);
}
static void case_truncated_frames(void)
{
 static const u8 nonce[32] = {0x05};
 u8 frame[256]; reset_case(); send_m1_expect(1, nonce);
 size_t valid_len = build_frame(frame, 4, 2, NULL, NULL, 0, 0, 0);
 for (size_t n = 0; n < valid_len; ++n) deliver(frame, n);
 CHECK(tx_count == 1);
 size_t bad_len = build_frame(frame, 4, 2, NULL, NULL, 0, 0, 1);
 unsigned before = tx_count;
 deliver(frame, bad_len);
 CHECK(tx_count == before);
 frame[14 + 2] = 0; frame[14 + 3] = 0xff;
 deliver(frame, bad_len);
 CHECK(tx_count == before);
}
int main(void)
{
 case_normal_retransmits_replays_and_group();
 case_new_m1_lower_counter();
 case_malformed_group_no_key_install();
 case_truncated_frames();
 puts("eapol_flow_test: all passed");
 return 0;
}
