/*
 * Host test of Source/AirLayer/AWDLBaOrig.h.
 * clang++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined Tools/baorig_test.cpp -o /tmp/baorig_test
 */
#include "../Source/AirLayer/AWDLBaOrig.h"
#include <stdio.h>

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

static const uint8_t A[6] = { 2, 1, 1, 1, 1, 1 }, B[6] = { 2, 2, 2, 2, 2, 2 };
static const uint8_t OWN[6] = { 2, 9, 9, 9, 9, 9 }, BSSID[6] = { 0x00, 0x25, 0x00, 0xFF, 0x94, 0x73 };
static const uint16_t OK_PARAMS = 2 | 0 << 2 | 64 << 6;

/* queues n frames 1 ms apart; returns how many asked for a request */
static int run(AwdlBaoTab *t, const uint8_t *mac, uint64_t *now, int n, AwdlBaoPeer **p)
{
	int req = 0;
	for (int i = 0; i < n; i++) {
		*now += 1000;
		if (awdlBaoQueued(t, mac, *now, p) == AWDL_BAO_DO_REQUEST)
			req++;
	}
	return req;
}

int main()
{
	uint64_t now = 5000000;
	AwdlBaoPeer *p = nullptr;

	/* A few frames never ask. A run of 16 in a second asks once. */
	{
		AwdlBaoTab t = {};
		CHECK(run(&t, A, &now, 15, &p) == 0 && p->state == AWDL_BAO_NONE);
		now += 2000000;
		CHECK(run(&t, A, &now, 15, &p) == 0);
		CHECK(run(&t, A, &now, 1, &p) == 1 && p->state == AWDL_BAO_ASKED && p->token);
		CHECK(run(&t, A, &now, 100, &p) == 0);
		CHECK(!awdlBaoSend(&t, A));

		/* The request frame. */
		uint8_t f[AWDL_BAO_REQUEST_LEN];
		uint16_t ssn = p->seq;
		CHECK(awdlBaoBuildRequest(f, p, OWN, BSSID) == 33);
		CHECK(f[0] == 0xD0 && !memcmp(f + 4, A, 6) && !memcmp(f + 10, OWN, 6) && !memcmp(f + 16, BSSID, 6));
		CHECK(f[24] == 3 && f[25] == 0 && f[26] == p->token);
		uint16_t params = (uint16_t)(f[27] | f[28] << 8);
		CHECK((params & 2) && !(params & 1) && ((params >> 2) & 0xF) == 0 && (params >> 6) == 64);
		CHECK(f[29] == 0 && f[30] == 0 && (uint16_t)(f[31] | f[32] << 8) == (uint16_t)(ssn << 4));

		/* A wrong token or another peer changes nothing; the answer opens it. */
		CHECK(!awdlBaoResponse(&t, A, (uint8_t)(p->token + 1), 0, OK_PARAMS, now));
		CHECK(!awdlBaoResponse(&t, B, p->token, 0, OK_PARAMS, now));
		CHECK(awdlBaoResponse(&t, A, p->token, 0, OK_PARAMS, now) && p->state == AWDL_BAO_ACTIVE);
		CHECK(!awdlBaoResponse(&t, A, p->token, 0, OK_PARAMS, now));

		/* The first QoS frame carries the request's starting sequence, and they count up. */
		AwdlBaoPeer *s = awdlBaoSend(&t, A);
		CHECK(s == p && s->seq == ssn);
		uint8_t plain[60] = { 0x08, 0 }, qos[64];
		for (int i = 24; i < 60; i++)
			plain[i] = (uint8_t)i;
		size_t n = awdlBaoQos(plain, sizeof(plain), s->seq, qos, sizeof(qos));
		CHECK(n == 62 && qos[0] == 0x88 && (uint16_t)(qos[22] | qos[23] << 8) >> 4 == ssn);
		CHECK(qos[24] == 0 && qos[25] == 0 && !memcmp(qos + 26, plain + 24, 36));
		CHECK(awdlBaoQos(plain, sizeof(plain), 1, qos, 61) == 0);
		plain[0] = 0xD0;
		CHECK(awdlBaoQos(plain, sizeof(plain), 1, qos, sizeof(qos)) == 0);
		awdlBaoSent(s, now);
		CHECK(s->seq == ((ssn + 1) & 0xFFF));
		s->seq = 0xFFF;
		awdlBaoSent(s, now);
		CHECK(s->seq == 0);

		/* The peer closes it: single frames for 10 s, then a run asks again. */
		awdlBaoDelba(&t, A, 5, now);
		CHECK(p->state == AWDL_BAO_ACTIVE);
		awdlBaoDelba(&t, A, 0, now);
		CHECK(p->state == AWDL_BAO_REFUSED && !awdlBaoSend(&t, A));
		CHECK(run(&t, A, &now, 500, &p) == 0);
		now += AWDL_BAO_DELBA_BACKOFF_US;
		uint16_t before = p->seq;
		CHECK(run(&t, A, &now, 16, &p) == 1 && p->seq == before);
	}

	/* No answer: three requests half a second apart, then a short pause of single frames and a new round. */
	{
		AwdlBaoTab t = {};
		int req = 0;
		for (int i = 0; i < 4000; i++) {
			now += 1000;
			req += awdlBaoQueued(&t, A, now, &p) == AWDL_BAO_DO_REQUEST;
		}
		CHECK(req == 3 && p->state == AWDL_BAO_REFUSED && t.noAnswer == 1);
		uint8_t tok = p->token;
		CHECK(!awdlBaoResponse(&t, A, tok, 0, OK_PARAMS, now));
		/* the pause runs from the last request's deadline, not from here */
		uint64_t until = p->untilUs;
		CHECK(until > now && until - now < AWDL_BAO_SILENT_BACKOFF_US);
		now = until - 20000;
		CHECK(run(&t, A, &now, 16, &p) == 0 && p->state == AWDL_BAO_REFUSED);
		now = until;
		CHECK(run(&t, A, &now, 16, &p) == 1 && p->state == AWDL_BAO_ASKED && p->token != tok);
		CHECK(awdlBaoResponse(&t, A, p->token, 0, OK_PARAMS, now) && p->state == AWDL_BAO_ACTIVE);
		CHECK(AWDL_BAO_SILENT_BACKOFF_US < AWDL_BAO_BACKOFF_US);
	}

	/* A refusal, and an answer that is not immediate Block Ack on TID 0. */
	{
		AwdlBaoTab t = {};
		run(&t, A, &now, 16, &p);
		CHECK(!awdlBaoResponse(&t, A, p->token, 37, OK_PARAMS, now) && p->state == AWDL_BAO_REFUSED);
		AwdlBaoTab u = {};
		run(&u, A, &now, 16, &p);
		CHECK(!awdlBaoResponse(&u, A, p->token, 0, 0 | 64 << 6, now));
		AwdlBaoTab v = {};
		run(&v, A, &now, 16, &p);
		CHECK(!awdlBaoResponse(&v, A, p->token, 0, 2 | 3 << 2 | 64 << 6, now));
		AwdlBaoTab w = {};
		run(&w, A, &now, 16, &p);
		CHECK(awdlBaoResponse(&w, A, p->token, 0, 2 | 256 << 6, now) && p->win == 64);
	}

	/* Watch: data out and nothing back closes it; anything back keeps it. */
	{
		AwdlBaoTab t = {};
		uint8_t mac[6];
		run(&t, A, &now, 16, &p);
		awdlBaoResponse(&t, A, p->token, 0, OK_PARAMS, now);
		for (int i = 0; i < 100; i++)
			awdlBaoSent(p, now);
		awdlBaoHeard(&t, A);
		now += AWDL_BAO_WATCH_US;
		CHECK(awdlBaoWatch(&t, now, mac) == AWDL_BAO_DO_NOTHING && p->state == AWDL_BAO_ACTIVE);
		for (int i = 0; i < 63; i++)
			awdlBaoSent(p, now);
		now += AWDL_BAO_WATCH_US;
		CHECK(awdlBaoWatch(&t, now, mac) == AWDL_BAO_DO_NOTHING);
		for (int i = 0; i < 64; i++)
			awdlBaoSent(p, now);
		CHECK(awdlBaoWatch(&t, now + 1000, mac) == AWDL_BAO_DO_NOTHING);
		now += AWDL_BAO_WATCH_US;
		CHECK(awdlBaoWatch(&t, now, mac) == AWDL_BAO_DO_DELBA && !memcmp(mac, A, 6));
		CHECK(p->state == AWDL_BAO_REFUSED && t.broken == 1 && !awdlBaoSend(&t, A));
		uint8_t f[AWDL_BAO_DELBA_LEN];
		CHECK(awdlBaoBuildDelba(f, mac, OWN, BSSID, 39) == 30 && f[24] == 3 && f[25] == 2);
		uint16_t params = (uint16_t)(f[26] | f[27] << 8);
		CHECK((params & 0x0800) && (params >> 12) == 0 && f[28] == 39 && f[29] == 0);

		/* With the watch off the same silence is left alone. */
		AwdlBaoTab u = {};
		u.watchOff = true;
		run(&u, A, &now, 16, &p);
		awdlBaoResponse(&u, A, p->token, 0, OK_PARAMS, now);
		for (int i = 0; i < 500; i++)
			awdlBaoSent(p, now);
		now += AWDL_BAO_WATCH_US;
		CHECK(awdlBaoWatch(&u, now, mac) == AWDL_BAO_DO_NOTHING && p->state == AWDL_BAO_ACTIVE);
	}

	/* A session unused for 10 s is asked for again, with the sequence going on. */
	{
		AwdlBaoTab t = {};
		run(&t, A, &now, 16, &p);
		awdlBaoResponse(&t, A, p->token, 0, OK_PARAMS, now);
		awdlBaoSent(p, now);
		uint16_t seq = p->seq;
		now += AWDL_BAO_IDLE_US + 1;
		CHECK(run(&t, A, &now, 1, &p) == 0 && p->state == AWDL_BAO_NONE && t.idle == 1);
		CHECK(run(&t, A, &now, 15, &p) == 1 && p->seq == seq);
	}

	/* Five peers in four entries: the live session stays, the oldest other goes. */
	{
		AwdlBaoTab t = {};
		uint8_t m[5][6];
		for (uint8_t i = 0; i < 5; i++) {
			uint8_t x[6] = { 2, 0, 0, 0, 0, i };
			memcpy(m[i], x, 6);
		}
		run(&t, m[0], &now, 16, &p);
		awdlBaoResponse(&t, m[0], p->token, 0, OK_PARAMS, now);
		for (int i = 1; i < 5; i++)
			run(&t, m[i], &now, 2, &p);
		CHECK(awdlBaoSend(&t, m[0]) != nullptr);
		CHECK(!awdlBaoFind(&t, m[1]) && awdlBaoFind(&t, m[4]));
	}

	/* The token is never 0. */
	{
		AwdlBaoTab t = {};
		t.nextToken = 255;
		run(&t, A, &now, 16, &p);
		CHECK(p->token == 1);
	}

	printf(fails ? "%d FAILED\n" : "baorig: all passed\n", fails);
	return fails != 0;
}
