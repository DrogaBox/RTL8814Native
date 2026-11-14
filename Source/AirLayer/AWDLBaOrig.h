/*
 * RTL8814AirLayer - Block Ack originator for AWDL data.
 *
 * To send A-MPDUs to a peer the layer needs a Block Ack agreement with it:
 * an ADDBA request, the peer's answer, and from then on QoS data frames
 * numbered in order. The radio joins those frames into A-MPDUs and handles the
 * BlockAck frames and the retries (Source/Driver/AWDLTx.c). This file keeps the
 * agreement of each peer and builds the frames. One TID, 0.
 *
 * A request goes out once a peer gets a run of unicast data, so a few packets
 * of discovery never start one. A peer that does not answer, refuses, or tears
 * the session down is left alone for a while and its data goes as single
 * frames, as before. The radio reports nothing about what it sent, and a
 * request held for a peer that is away is dropped once it is too old, so no
 * answer does not mean the peer refused: it is asked again after a short
 * pause, a refusal after a long one. A session that carries data out while
 * nothing comes back from the peer is taken for broken and closed.
 *
 * No kernel types, so Tools/baorig_test.cpp runs it on the host. The caller
 * serialises the calls.
 */
#ifndef AWDL_BA_ORIG_H
#define AWDL_BA_ORIG_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AWDL_BAO_PEERS            4
#define AWDL_BAO_TID              0
#define AWDL_BAO_WIN              64
#define AWDL_BAO_BURST            16          /* unicast data frames in AWDL_BAO_BURST_US that start a request */
#define AWDL_BAO_BURST_US         1000000ULL
#define AWDL_BAO_ASK_US           500000ULL   /* how long an answer may take */
#define AWDL_BAO_TRIES            3
#define AWDL_BAO_BACKOFF_US       60000000ULL /* after a refusal or a broken session */
#define AWDL_BAO_SILENT_BACKOFF_US 5000000ULL /* after no answer: silence is not a refusal, the request may never have reached the peer */
#define AWDL_BAO_DELBA_BACKOFF_US 10000000ULL /* after the peer closed the session */
#define AWDL_BAO_IDLE_US          10000000ULL /* a session unused this long is asked for again */
#define AWDL_BAO_WATCH_US         2000000ULL
#define AWDL_BAO_WATCH_TX         64          /* frames out in a watch period with none back: broken */

#define AWDL_BAO_HDR              24
#define AWDL_BAO_REQUEST_LEN      (AWDL_BAO_HDR + 9)
#define AWDL_BAO_DELBA_LEN        (AWDL_BAO_HDR + 6)

enum { AWDL_BAO_NONE, AWDL_BAO_ASKED, AWDL_BAO_ACTIVE, AWDL_BAO_REFUSED };
enum { AWDL_BAO_DO_NOTHING, AWDL_BAO_DO_REQUEST, AWDL_BAO_DO_DELBA };

struct AwdlBaoPeer {
	uint8_t  mac[6];
	uint8_t  state;
	uint8_t  token;
	uint8_t  tries;
	uint16_t seq;             /* of the next QoS frame; the request's starting sequence */
	uint16_t win;
	uint64_t usedUs;          /* 0 = entry free */
	uint64_t askUs, untilUs, burstUs, watchUs;
	uint32_t burst, watchTx, watchRx;
};

struct AwdlBaoTab {
	AwdlBaoPeer peer[AWDL_BAO_PEERS];
	uint8_t  nextToken;
	bool     watchOff;        /* never close a session for silence */
	uint64_t requests, accepted, refused, noAnswer, delbaIn, broken, idle;
};

static inline AwdlBaoPeer *awdlBaoFind(AwdlBaoTab *t, const uint8_t *mac)
{
	for (unsigned i = 0; i < AWDL_BAO_PEERS; i++)
		if (t->peer[i].usedUs && !memcmp(t->peer[i].mac, mac, 6))
			return &t->peer[i];
	return nullptr;
}

/*
 * One unicast data frame for mac entered the queue. AWDL_BAO_DO_REQUEST: send
 * an ADDBA request built from *out. now is never 0.
 */
static inline int awdlBaoQueued(AwdlBaoTab *t, const uint8_t *mac, uint64_t now, AwdlBaoPeer **out)
{
	AwdlBaoPeer *p = awdlBaoFind(t, mac);
	if (!p) {
		/* A free entry, else the one unused longest; a live session only
		 * when there is nothing else. */
		AwdlBaoPeer *best = nullptr;
		for (unsigned i = 0; i < AWDL_BAO_PEERS; i++) {
			AwdlBaoPeer *q = &t->peer[i];
			if (!q->usedUs) {
				best = q;
				break;
			}
			bool qLive = q->state == AWDL_BAO_ACTIVE, bLive = best && best->state == AWDL_BAO_ACTIVE;
			if (!best || (bLive && !qLive) || (bLive == qLive && q->usedUs < best->usedUs))
				best = q;
		}
		p = best;
		memset(p, 0, sizeof(*p));
		memcpy(p->mac, mac, 6);
		p->seq = (uint16_t)((now >> 6) & 0xFFF);
	}
	if (out)
		*out = p;
	int act = AWDL_BAO_DO_NOTHING;
	if (p->state == AWDL_BAO_REFUSED && now >= p->untilUs)
		p->state = AWDL_BAO_NONE;
	if (p->state == AWDL_BAO_ACTIVE && p->usedUs && now - p->usedUs > AWDL_BAO_IDLE_US) {
		p->state = AWDL_BAO_NONE;
		p->burst = 0;
		t->idle++;
	}
	if (p->state == AWDL_BAO_NONE) {
		if (!p->burst || now - p->burstUs > AWDL_BAO_BURST_US) {
			p->burstUs = now;
			p->burst = 0;
		}
		if (++p->burst >= AWDL_BAO_BURST) {
			p->state = AWDL_BAO_ASKED;
			p->tries = 1;
			p->burst = 0;
			if (++t->nextToken == 0)
				t->nextToken = 1;
			p->token = t->nextToken;
			p->askUs = now;
			t->requests++;
			act = AWDL_BAO_DO_REQUEST;
		}
	} else if (p->state == AWDL_BAO_ASKED && now - p->askUs > AWDL_BAO_ASK_US) {
		if (p->tries < AWDL_BAO_TRIES) {
			p->tries++;
			p->askUs = now;
			t->requests++;
			act = AWDL_BAO_DO_REQUEST;
		} else {
			p->state = AWDL_BAO_REFUSED;
			p->untilUs = now + AWDL_BAO_SILENT_BACKOFF_US;
			t->noAnswer++;
		}
	}
	p->usedUs = now;
	return act;
}

/* The peer's ADDBA response. True when the session is on from now. */
static inline bool awdlBaoResponse(AwdlBaoTab *t, const uint8_t *mac, uint8_t token, uint16_t status,
                                   uint16_t params, uint64_t now)
{
	AwdlBaoPeer *p = awdlBaoFind(t, mac);
	if (!p || p->state != AWDL_BAO_ASKED || p->token != token)
		return false;
	uint16_t win = params >> 6;
	if (status == 0 && (params & 2) && ((params >> 2) & 0xF) == AWDL_BAO_TID && win) {
		p->state = AWDL_BAO_ACTIVE;
		p->win = win > AWDL_BAO_WIN ? AWDL_BAO_WIN : win;
		p->watchUs = now;
		p->watchTx = p->watchRx = 0;
		p->usedUs = now;
		t->accepted++;
		return true;
	}
	p->state = AWDL_BAO_REFUSED;
	p->untilUs = now + AWDL_BAO_BACKOFF_US;
	t->refused++;
	return false;
}

/* A DELBA from the peer for a session of ours (its initiator bit clear). */
static inline void awdlBaoDelba(AwdlBaoTab *t, const uint8_t *mac, uint8_t tid, uint64_t now)
{
	AwdlBaoPeer *p = awdlBaoFind(t, mac);
	if (!p || tid != AWDL_BAO_TID || (p->state != AWDL_BAO_ACTIVE && p->state != AWDL_BAO_ASKED))
		return;
	p->state = AWDL_BAO_REFUSED;
	p->untilUs = now + AWDL_BAO_DELBA_BACKOFF_US;
	t->delbaIn++;
}

/* Before a frame to mac goes to the radio: the entry when it goes as QoS data
 * with entry->seq, else null. */
static inline AwdlBaoPeer *awdlBaoSend(AwdlBaoTab *t, const uint8_t *mac)
{
	AwdlBaoPeer *p = awdlBaoFind(t, mac);
	return p && p->state == AWDL_BAO_ACTIVE ? p : nullptr;
}

/* The radio took that QoS frame. */
static inline void awdlBaoSent(AwdlBaoPeer *p, uint64_t now)
{
	p->seq = (uint16_t)((p->seq + 1) & 0xFFF);
	p->watchTx++;
	p->usedUs = now;
}

/* A data frame from mac arrived. */
static inline void awdlBaoHeard(AwdlBaoTab *t, const uint8_t *mac)
{
	AwdlBaoPeer *p = awdlBaoFind(t, mac);
	if (p)
		p->watchRx++;
}

/* Call a few times a second. AWDL_BAO_DO_DELBA: the session with mac is closed
 * here; send it a DELBA. One per call. */
static inline int awdlBaoWatch(AwdlBaoTab *t, uint64_t now, uint8_t *mac)
{
	for (unsigned i = 0; i < AWDL_BAO_PEERS; i++) {
		AwdlBaoPeer *p = &t->peer[i];
		if (!p->usedUs || p->state != AWDL_BAO_ACTIVE || now - p->watchUs < AWDL_BAO_WATCH_US)
			continue;
		bool bad = !t->watchOff && p->watchTx >= AWDL_BAO_WATCH_TX && !p->watchRx;
		p->watchUs = now;
		p->watchTx = p->watchRx = 0;
		if (bad) {
			p->state = AWDL_BAO_REFUSED;
			p->untilUs = now + AWDL_BAO_BACKOFF_US;
			t->broken++;
			memcpy(mac, p->mac, 6);
			return AWDL_BAO_DO_DELBA;
		}
	}
	return AWDL_BAO_DO_NOTHING;
}

static inline void awdlBaoHeader(uint8_t *f, const uint8_t *peer, const uint8_t *own, const uint8_t *bssid)
{
	memset(f, 0, AWDL_BAO_HDR);
	f[0] = 0xD0;
	memcpy(f + 4, peer, 6);
	memcpy(f + 10, own, 6);
	memcpy(f + 16, bssid, 6);
}

/* ADDBA request: immediate Block Ack, no A-MSDU, no timeout. */
static inline size_t awdlBaoBuildRequest(uint8_t *f, const AwdlBaoPeer *p, const uint8_t *own,
                                         const uint8_t *bssid)
{
	awdlBaoHeader(f, p->mac, own, bssid);
	uint8_t *b = f + AWDL_BAO_HDR;
	uint16_t params = (uint16_t)(2u | AWDL_BAO_TID << 2 | AWDL_BAO_WIN << 6);
	uint16_t ssc = (uint16_t)(p->seq << 4);
	b[0] = 3;
	b[1] = 0;
	b[2] = p->token;
	b[3] = (uint8_t)params;
	b[4] = (uint8_t)(params >> 8);
	b[5] = b[6] = 0;
	b[7] = (uint8_t)ssc;
	b[8] = (uint8_t)(ssc >> 8);
	return AWDL_BAO_REQUEST_LEN;
}

/* DELBA from the originator. */
static inline size_t awdlBaoBuildDelba(uint8_t *f, const uint8_t *peer, const uint8_t *own,
                                       const uint8_t *bssid, uint16_t reason)
{
	awdlBaoHeader(f, peer, own, bssid);
	uint8_t *b = f + AWDL_BAO_HDR;
	uint16_t params = (uint16_t)(1u << 11 | AWDL_BAO_TID << 12);
	b[0] = 3;
	b[1] = 2;
	b[2] = (uint8_t)params;
	b[3] = (uint8_t)(params >> 8);
	b[4] = (uint8_t)reason;
	b[5] = (uint8_t)(reason >> 8);
	return AWDL_BAO_DELBA_LEN;
}

/* A plain data frame (24-byte header) as QoS data of the TID with seq. Returns
 * the new length, or 0 when it is not such a frame or does not fit. */
static inline size_t awdlBaoQos(const uint8_t *in, size_t len, uint16_t seq, uint8_t *out, size_t cap)
{
	if (len < AWDL_BAO_HDR || len + 2 > cap || (in[0] & 0xFC) != 0x08)
		return 0;
	memcpy(out, in, AWDL_BAO_HDR);
	out[0] = 0x88;
	out[22] = (uint8_t)(seq << 4);
	out[23] = (uint8_t)(seq >> 4);
	out[24] = AWDL_BAO_TID;
	out[25] = 0;
	memcpy(out + 26, in + AWDL_BAO_HDR, len - AWDL_BAO_HDR);
	return len + 2;
}

#endif
