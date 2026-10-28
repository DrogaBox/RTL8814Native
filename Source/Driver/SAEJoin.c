/*
 * RTL8814Native — SAE join state machine (vendor-tree side)
 *
 * Threading: the join (non-worker thread) stores the passphrase and posts a
 * start event; the receive path copies authentication frames into a small
 * ring; the link timer sets a flag. Everything else, including every call
 * into SAE.c, runs on the SAE worker, which is the only owner of the exchange
 * context. A generation counter makes work left over from an earlier join
 * harmless: each event carries the generation it was posted under and the
 * worker drops it when the join has moved on.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include <drv_types.h>
#include <hal_data.h>
#include <linux/random.h>
#include "RTLJoinLog.h"
#include "SAE.h"
#include "SAEAuth.h"
#include "SAEJoin.h"

#define SAE_RX_SLOTS       4
#define SAE_RX_MAX         (RTL_SAE_COMMIT_MAX_LEN + 16)
#define SAE_RETX_MS        500
#define SAE_MAX_RETRIES    4
#define SAE_MAX_TOKENS     2
#define SAE_AUTH_HDR       6            /* algorithm, sequence, status */

#define SAE_STATUS_TOKEN   76           /* anti-clogging token required */
#define SAE_STATUS_GROUP   77           /* finite cyclic group not supported */

struct sae_rx_slot {
	u32 gen;
	u16 len;
	u8 data[SAE_RX_MAX];
};

/* State shared between threads; guarded by lock. */
static struct {
	_lock lock;
	u8 inited;
	_adapter *padapter;
	u32 gen;
	u8 active;
	u8 pw[RTL_SAE_PASSWORD_MAX_LEN];
	u8 pw_len;
	u8 ev_start;
	u8 ev_timeout;
	u32 start_gen;
	struct sae_rx_slot rx[SAE_RX_SLOTS];
	u8 rx_head, rx_count;
	/* Result for the association request and the 4-way handshake. */
	u8 assoc_ready;
	u8 assoc_ie[64];
	u8 assoc_ie_len;
	u8 pmk_valid;
	u8 pmk[RTL_SAE_PMK_LEN];
	u8 pmkid[RTL_SAE_PMKID_LEN];
	struct work_struct work;
} g_sae;

enum { SAE_PH_IDLE = 0, SAE_PH_COMMITTED, SAE_PH_CONFIRMED, SAE_PH_DONE };

/* Worker-owned state: only sae_work() and its callees touch it. */
static struct {
	struct rtl_sae_p256 *ctx;
	u32 gen;
	int phase;
	u8 retries;
	u8 tokens;
	struct {
		u16 seq, status, len;
		u8 body[RTL_SAE_COMMIT_MAX_LEN];
	} last;
	struct sae_rx_slot frame;      /* the frame being handled */
	u8 pw[RTL_SAE_PASSWORD_MAX_LEN];
	u8 pw_len;
} w_sae;

static void sae_step(int step, int result)
{
	rtl8814native_jlog(JL_SAE_STEP, step, (u32)result);
}

/* Drop the exchange context but keep the passphrase copy: the start event has
 * just filled it and the exchange is about to be built from it. */
static void sae_drop_ctx(void)
{
	if (w_sae.ctx) {
		rtl_sae_p256_clear(w_sae.ctx);
		rtw_mfree(w_sae.ctx, sizeof(*w_sae.ctx));
		w_sae.ctx = NULL;
	}
	_rtw_memset(&w_sae.last, 0, sizeof(w_sae.last));
	w_sae.phase = SAE_PH_IDLE;
}

static void sae_wipe_worker(void)
{
	sae_drop_ctx();
	_rtw_memset(w_sae.pw, 0, sizeof(w_sae.pw));
	w_sae.pw_len = 0;
}

static void sae_wipe_shared_locked(void)
{
	_rtw_memset(g_sae.pw, 0, sizeof(g_sae.pw));
	g_sae.pw_len = 0;
	_rtw_memset(g_sae.pmk, 0, sizeof(g_sae.pmk));
	_rtw_memset(g_sae.pmkid, 0, sizeof(g_sae.pmkid));
	_rtw_memset(g_sae.assoc_ie, 0, sizeof(g_sae.assoc_ie));
	g_sae.assoc_ie_len = 0;
	g_sae.assoc_ready = 0;
	g_sae.pmk_valid = 0;
	g_sae.ev_start = 0;
	g_sae.ev_timeout = 0;
	g_sae.rx_head = g_sae.rx_count = 0;
}

/* AP's RSN IE: group data cipher type and whether it requires PMF. */
static void sae_parse_ap_rsn(_adapter *padapter, u8 *group_cipher, int *pmf_required)
{
	struct mlme_ext_info *info = &padapter->mlmeextpriv.mlmext_info;
	int ielen = 0;
	u8 *p;
	uint total = info->network.IELength;

	*group_cipher = 4;      /* CCMP */
	*pmf_required = 0;
	if (total <= _BEACON_IE_OFFSET_)
		return;
	p = rtw_get_ie(info->network.IEs + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ielen,
		       total - _BEACON_IE_OFFSET_);
	if (!p || ielen < 8)
		return;
	/* p[0] id, p[1] length, p[2..3] version, p[4..7] group suite */
	if (p[4] == 0x00 && p[5] == 0x0f && p[6] == 0xac)
		*group_cipher = p[7];
	{
		int pos = 8, n;
		if (pos + 2 > ielen)
			return;
		n = p[2 + pos] | (p[2 + pos + 1] << 8);
		pos += 2 + 4 * n;
		if (pos + 2 > ielen)
			return;
		n = p[2 + pos] | (p[2 + pos + 1] << 8);
		pos += 2 + 4 * n;
		if (pos + 2 > ielen)
			return;
		*pmf_required = (p[2 + pos] & 0x40) != 0;
	}
}

static int sae_stale(_adapter *padapter)
{
	struct mlme_ext_priv *ext = &padapter->mlmeextpriv;
	struct mlme_ext_info *info = &ext->mlmext_info;
	_irqL irqL;
	int stale;

	_enter_critical_bh(&g_sae.lock, &irqL);
	stale = !g_sae.active || g_sae.gen != w_sae.gen;
	_exit_critical_bh(&g_sae.lock, &irqL);
	if (!stale)
		stale = ext->join_abort || !(info->state & WIFI_FW_AUTH_STATE);
	if (stale)
		sae_step(RTL_SAE_STEP_STALE, 1);
	return stale;
}

/* End the join with a failure, as the auth-retry timeout does. */
static void sae_fail(_adapter *padapter, u16 status)
{
	struct mlme_ext_priv *ext = &padapter->mlmeextpriv;
	struct mlme_ext_info *info = &ext->mlmext_info;
	_irqL irqL;
	int ours;

	_enter_critical_bh(&g_sae.lock, &irqL);
	ours = g_sae.active && g_sae.gen == w_sae.gen;
	if (ours) {
		g_sae.active = 0;
		sae_wipe_shared_locked();
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	sae_wipe_worker();
	if (!ours)
		return;
	sae_step(RTL_SAE_STEP_FAIL, status);
	_cancel_timer_ex(&ext->link_timer);
	if (info->state & WIFI_FW_AUTH_STATE) {
		info->state = 0;
		report_join_res(padapter, -1, status ? status : WLAN_STATUS_UNSPECIFIED_FAILURE);
	}
}

static void sae_tx(_adapter *padapter, u16 seq, u16 status, const u8 *body, size_t len)
{
	int rc;

	if (len > sizeof(w_sae.last.body))
		len = 0;
	w_sae.last.seq = seq;
	w_sae.last.status = status;
	w_sae.last.len = (u16)len;
	if (len && body != w_sae.last.body)
		_rtw_memcpy(w_sae.last.body, body, len);
	rc = rtl_sae_issue_auth(padapter, seq, status, body, len);
	sae_step(seq == 1 ? RTL_SAE_STEP_COMMIT_TX : RTL_SAE_STEP_CONFIRM_TX, rc);
	set_link_timer(&padapter->mlmeextpriv, SAE_RETX_MS);
}

static void sae_send_commit(_adapter *padapter)
{
	size_t len = 0;
	int rc;

	rc = rtl_sae_p256_write_commit(w_sae.ctx, w_sae.last.body, sizeof(w_sae.last.body), &len);
	if (rc) {
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	sae_tx(padapter, 1, 0, w_sae.last.body, len);
}

static void sae_handle_start(_adapter *padapter)
{
	u32 t0;
	int rc;

	sae_drop_ctx();
	w_sae.ctx = rtw_zmalloc(sizeof(*w_sae.ctx));
	if (!w_sae.ctx) {
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	t0 = rtw_get_current_time();
	rc = rtl_sae_p256_start(w_sae.ctx, w_sae.pw, w_sae.pw_len,
				adapter_mac_addr(padapter),
				get_my_bssid(&padapter->mlmeextpriv.mlmext_info.network),
				NULL, 0, rtl_sae_entropy_fill, NULL);
	_rtw_memset(w_sae.pw, 0, sizeof(w_sae.pw));
	w_sae.pw_len = 0;
	sae_step(RTL_SAE_STEP_COMPUTE_MS, (int)rtw_get_passing_time_ms(t0));
	sae_step(RTL_SAE_STEP_START, rc);
	if (rc) {
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	if (sae_stale(padapter)) {
		sae_wipe_worker();
		return;
	}
	w_sae.phase = SAE_PH_COMMITTED;
	w_sae.retries = 0;
	w_sae.tokens = 0;
	sae_send_commit(padapter);
}

static void sae_send_confirm(_adapter *padapter)
{
	u8 body[RTL_SAE_CONFIRM_LEN];
	int rc = rtl_sae_p256_write_confirm(w_sae.ctx, body);

	if (rc) {
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	sae_tx(padapter, 2, 0, body, sizeof(body));
}

/* The exchange is confirmed: take the keys and go on to association. */
static void sae_complete(_adapter *padapter)
{
	u8 pmk[RTL_SAE_PMK_LEN], pmkid[RTL_SAE_PMKID_LEN];
	u8 ie[64], group;
	int pmf_req, rc, ielen;
	_irqL irqL;

	rc = rtl_sae_p256_finish(w_sae.ctx, pmk, pmkid);
	sae_step(RTL_SAE_STEP_KEYS, rc);
	rtw_mfree(w_sae.ctx, sizeof(*w_sae.ctx));
	w_sae.ctx = NULL;
	if (rc) {
		_rtw_memset(pmk, 0, sizeof(pmk));
		_rtw_memset(pmkid, 0, sizeof(pmkid));
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	sae_parse_ap_rsn(padapter, &group, &pmf_req);
	ielen = rtl_sae_build_rsn_ie(group, pmf_req, pmkid, ie, sizeof(ie));
	if (ielen <= 0 || sae_stale(padapter)) {
		_rtw_memset(pmk, 0, sizeof(pmk));
		_rtw_memset(pmkid, 0, sizeof(pmkid));
		if (ielen <= 0)
			sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		else
			sae_wipe_worker();
		return;
	}

	_enter_critical_bh(&g_sae.lock, &irqL);
	_rtw_memcpy(g_sae.pmk, pmk, sizeof(pmk));
	_rtw_memcpy(g_sae.pmkid, pmkid, sizeof(pmkid));
	g_sae.pmk_valid = 1;
	_rtw_memcpy(g_sae.assoc_ie, ie, ielen);
	g_sae.assoc_ie_len = (u8)ielen;
	g_sae.assoc_ready = 1;
	g_sae.active = 0;       /* the exchange is over; keys stay for step 4 */
	_exit_critical_bh(&g_sae.lock, &irqL);
	_rtw_memset(pmk, 0, sizeof(pmk));
	_rtw_memset(pmkid, 0, sizeof(pmkid));
	_rtw_memset(ie, 0, sizeof(ie));
	w_sae.phase = SAE_PH_DONE;
	sae_wipe_worker();

	sae_step(RTL_SAE_STEP_ASSOC, ielen);
	start_clnt_assoc(padapter);
}

static void sae_handle_rx(_adapter *padapter)
{
	const u8 *f = w_sae.frame.data;
	u32 len = w_sae.frame.len;
	u16 seq, status;
	const u8 *body;
	u32 body_len;
	int rc;

	if (len < SAE_AUTH_HDR || !w_sae.ctx)
		return;
	seq = f[2] | (f[3] << 8);
	status = f[4] | (f[5] << 8);
	body = f + SAE_AUTH_HDR;
	body_len = len - SAE_AUTH_HDR;

	if (seq == 1 && status == SAE_STATUS_TOKEN && w_sae.phase == SAE_PH_COMMITTED) {
		/* group (2 bytes), then the token */
		if (body_len < 3 || (body[0] | (body[1] << 8)) != RTL_SAE_GROUP_P256 ||
		    body_len - 2 > RTL_SAE_TOKEN_MAX_LEN || ++w_sae.tokens > SAE_MAX_TOKENS) {
			sae_fail(padapter, SAE_STATUS_TOKEN);
			return;
		}
		sae_step(RTL_SAE_STEP_TOKEN, (int)(body_len - 2));
		if (rtl_sae_p256_set_token(w_sae.ctx, body + 2, body_len - 2)) {
			sae_fail(padapter, SAE_STATUS_TOKEN);
			return;
		}
		sae_send_commit(padapter);
		return;
	}
	if (status != 0) {
		/* 126 (H2E required) and 127 (SAE-PK) show up here plainly */
		sae_fail(padapter, status);
		return;
	}

	if (seq == 1) {
		if (w_sae.phase == SAE_PH_CONFIRMED) {
			/* the AP repeated its commit: repeat our confirm */
			sae_send_confirm(padapter);
			return;
		}
		if (w_sae.phase != SAE_PH_COMMITTED)
			return;
		{
			u32 t0 = rtw_get_current_time();
			rc = rtl_sae_p256_process_commit(w_sae.ctx, body, body_len, 0);
			sae_step(RTL_SAE_STEP_COMPUTE_MS, (int)rtw_get_passing_time_ms(t0));
		}
		sae_step(RTL_SAE_STEP_PEER_COMMIT, rc);
		if (rc) {
			sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
			return;
		}
		if (sae_stale(padapter)) {
			sae_wipe_worker();
			return;
		}
		w_sae.phase = SAE_PH_CONFIRMED;
		w_sae.retries = 0;
		sae_send_confirm(padapter);
	} else if (seq == 2) {
		if (w_sae.phase != SAE_PH_CONFIRMED)
			return;
		rc = rtl_sae_p256_check_confirm(w_sae.ctx, body, body_len);
		sae_step(RTL_SAE_STEP_PEER_CONFIRM, rc);
		if (rc) {
			sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
			return;
		}
		sae_complete(padapter);
	}
}

static void sae_handle_timeout(_adapter *padapter)
{
	if (!w_sae.ctx || (w_sae.phase != SAE_PH_COMMITTED && w_sae.phase != SAE_PH_CONFIRMED))
		return;
	if (++w_sae.retries > SAE_MAX_RETRIES) {
		sae_fail(padapter, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}
	sae_step(RTL_SAE_STEP_RETRY, w_sae.last.seq);
	sae_tx(padapter, w_sae.last.seq, w_sae.last.status, w_sae.last.body, w_sae.last.len);
}

enum { SAE_EV_NONE = 0, SAE_EV_START, SAE_EV_RX, SAE_EV_TIMEOUT, SAE_EV_SKIP };

static int sae_next_event(void)
{
	_irqL irqL;
	int ev = SAE_EV_NONE;

	_enter_critical_bh(&g_sae.lock, &irqL);
	if (g_sae.ev_start) {
		g_sae.ev_start = 0;
		w_sae.gen = g_sae.start_gen;
		_rtw_memcpy(w_sae.pw, g_sae.pw, sizeof(w_sae.pw));
		w_sae.pw_len = g_sae.pw_len;
		_rtw_memset(g_sae.pw, 0, sizeof(g_sae.pw));
		g_sae.pw_len = 0;
		ev = SAE_EV_START;
	} else if (g_sae.rx_count) {
		struct sae_rx_slot *s = &g_sae.rx[g_sae.rx_head];

		g_sae.rx_head = (g_sae.rx_head + 1) % SAE_RX_SLOTS;
		g_sae.rx_count--;
		if (s->gen == g_sae.gen && g_sae.active) {
			_rtw_memcpy(&w_sae.frame, s, sizeof(w_sae.frame));
			ev = SAE_EV_RX;
		} else {
			ev = SAE_EV_SKIP;           /* frame from an earlier join */
		}
	} else if (g_sae.ev_timeout) {
		g_sae.ev_timeout = 0;
		ev = SAE_EV_TIMEOUT;
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	return ev;
}

static void sae_work(struct work_struct *w)
{
	_adapter *padapter;
	_irqL irqL;
	int ev;

	(void)w;
	_enter_critical_bh(&g_sae.lock, &irqL);
	padapter = g_sae.padapter;
	_exit_critical_bh(&g_sae.lock, &irqL);
	if (!padapter)
		return;
	while ((ev = sae_next_event()) != SAE_EV_NONE) {
		switch (ev) {
		case SAE_EV_START:
			sae_handle_start(padapter);
			break;
		case SAE_EV_RX:
			if (!sae_stale(padapter))
				sae_handle_rx(padapter);
			else
				sae_wipe_worker();
			break;
		case SAE_EV_TIMEOUT:
			if (!sae_stale(padapter))
				sae_handle_timeout(padapter);
			else
				sae_wipe_worker();
			break;
		default:
			break;
		}
	}
}

/* ---- entry points used by the vendor MLME and the radio join ---- */

void rtl_sae_join_init(void)
{
	if (g_sae.inited)
		return;
	_rtw_spinlock_init(&g_sae.lock);
	INIT_WORK(&g_sae.work, sae_work);
	rtl_work_bind_sae(&g_sae.work);
	g_sae.inited = 1;
}

void rtl_sae_join_exit(void)
{
	if (g_sae.inited)
		rtl_sae_join_cancel(NULL);
}

int rtl_sae_join_active(_adapter *padapter)
{
	_irqL irqL;
	int active;

	if (!g_sae.inited)
		return 0;
	_enter_critical_bh(&g_sae.lock, &irqL);
	active = g_sae.active && g_sae.padapter == padapter;
	_exit_critical_bh(&g_sae.lock, &irqL);
	return active;
}

int rtl_sae_join_prepare(_adapter *padapter, const uint8_t *password, uint32_t password_len)
{
	static const u8 placeholder[22] = {
		0x30, 0x14, 0x01, 0x00,
		0x00, 0x0f, 0xac, 0x04,         /* group: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,   /* pairwise: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x08,   /* AKM: SAE */
		0xcc, 0x00                      /* MFPC | MFPR */
	};
	struct security_priv *psec;
	_irqL irqL;
	u8 probe;

	if (!padapter || !password || !password_len || password_len > RTL_SAE_PASSWORD_MAX_LEN)
		return -EINVAL;
	/* Fail closed when the kernel does not give us secure entropy */
	if (rtl_sae_entropy_fill(NULL, &probe, 1)) {
		sae_step(RTL_SAE_STEP_PREPARE, -EOPNOTSUPP);
		return -EOPNOTSUPP;
	}
	probe = 0;
	rtl_sae_join_init();

	_enter_critical_bh(&g_sae.lock, &irqL);
	g_sae.gen++;
	sae_wipe_shared_locked();
	_rtw_memcpy(g_sae.pw, password, password_len);
	g_sae.pw_len = (u8)password_len;
	g_sae.padapter = padapter;
	g_sae.active = 1;
	_exit_critical_bh(&g_sae.lock, &irqL);

	/* Give the join an RSN IE slot; the association hook replaces its
	 * contents with the real one (PMKID included) once SAE has finished. */
	psec = &padapter->securitypriv;
	rtw_reset_securitypriv(padapter);
	_rtw_memset(psec->supplicant_ie, 0, sizeof(psec->supplicant_ie));
	_rtw_memcpy(psec->supplicant_ie, placeholder, sizeof(placeholder));
	psec->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
	psec->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	psec->dot11PrivacyAlgrthm = _AES_;
	psec->dot118021XGrpPrivacy = _AES_;
	psec->ndisencryptstatus = Ndis802_11Encryption3Enabled;
	psec->binstallGrpkey = _FAIL;
	psec->busetkipkey = _FAIL;
	sae_step(RTL_SAE_STEP_PREPARE, 0);
	return 0;
}

void rtl_sae_join_start(_adapter *padapter)
{
	_irqL irqL;
	int ok = 0;

	_enter_critical_bh(&g_sae.lock, &irqL);
	if (g_sae.active && g_sae.padapter == padapter && g_sae.pw_len) {
		g_sae.start_gen = g_sae.gen;
		g_sae.ev_start = 1;
		ok = 1;
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	if (ok) {
		/* keep timeouts coming while the worker computes */
		set_link_timer(&padapter->mlmeextpriv, SAE_RETX_MS * 2);
		rtl_schedule_work(&g_sae.work);
	}
}

void rtl_sae_join_rx_auth(_adapter *padapter, const uint8_t *frame, uint32_t len)
{
	_irqL irqL;
	int queued = 0;

	if (!g_sae.inited || len < SAE_AUTH_HDR || len > SAE_RX_MAX ||
	    frame[0] != 3 || frame[1] != 0)
		return;
	_enter_critical_bh(&g_sae.lock, &irqL);
	if (g_sae.active && g_sae.padapter == padapter && g_sae.rx_count < SAE_RX_SLOTS) {
		struct sae_rx_slot *s = &g_sae.rx[(g_sae.rx_head + g_sae.rx_count) % SAE_RX_SLOTS];

		s->gen = g_sae.gen;
		s->len = (u16)len;
		_rtw_memcpy(s->data, frame, len);
		g_sae.rx_count++;
		queued = 1;
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	if (queued)
		rtl_schedule_work(&g_sae.work);
}

void rtl_sae_join_timeout(_adapter *padapter)
{
	_irqL irqL;
	int ok = 0;

	if (!g_sae.inited)
		return;
	_enter_critical_bh(&g_sae.lock, &irqL);
	if (g_sae.active && g_sae.padapter == padapter) {
		g_sae.ev_timeout = 1;
		ok = 1;
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	if (ok)
		rtl_schedule_work(&g_sae.work);
}

void rtl_sae_join_cancel(_adapter *padapter)
{
	_irqL irqL;

	(void)padapter;
	if (!g_sae.inited)
		return;
	_enter_critical_bh(&g_sae.lock, &irqL);
	g_sae.gen++;
	g_sae.active = 0;
	sae_wipe_shared_locked();
	/* wake the worker so it drops its own copy of the exchange */
	g_sae.ev_timeout = 1;
	_exit_critical_bh(&g_sae.lock, &irqL);
	rtl_schedule_work(&g_sae.work);
}

int rtl_sae_join_assoc_rsn(_adapter *padapter, uint8_t *out, uint32_t *len)
{
	_irqL irqL;
	int ready = 0;

	if (!g_sae.inited)
		return 0;
	_enter_critical_bh(&g_sae.lock, &irqL);
	if (g_sae.assoc_ready && g_sae.padapter == padapter) {
		_rtw_memcpy(out, g_sae.assoc_ie, g_sae.assoc_ie_len);
		*len = g_sae.assoc_ie_len;
		ready = 1;
	}
	_exit_critical_bh(&g_sae.lock, &irqL);
	return ready;
}
