#include "ReplayCheck.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static const uint8_t c0[8] = {0,0,0,0,0,0,0,0};
static const uint8_t c1[8] = {0,0,0,0,0,0,0,1};
static const uint8_t c2[8] = {0,0,0,0,0,0,0,2};
static const uint8_t c3[8] = {0,0,0,0,0,0,0,3};
static const uint8_t c4[8] = {0,0,0,0,0,0,0,4};
static const uint8_t pre32[8] = {0,0,0,0,0xff,0xff,0xff,0xff};
static const uint8_t post32[8] = {0,0,0,1,0,0,0,0};
static const uint8_t high32[8] = {0,0,0,1,0,0,0,1};

int main(void)
{
    struct rtl_replay_state state = {{0}, 0};

    /* 4-way order: M1 establishes the base, then M3 advances it. */
    CHECK(rtl_replay_check(&state, c1) == RTL_REPLAY_NEW);
    rtl_replay_begin(&state, c1); /* Every new M1 starts a fresh exchange. */
    CHECK(rtl_replay_check(&state, c1) == RTL_REPLAY_REPEAT);
    CHECK(rtl_replay_check(&state, c2) == RTL_REPLAY_NEW); /* M3 */
    rtl_replay_accept(&state, c2);

    /* Duplicate M3 is distinct from an older M3; neither advances the base. */
    CHECK(rtl_replay_check(&state, c2) == RTL_REPLAY_REPEAT);
    CHECK(rtl_replay_check(&state, c1) == RTL_REPLAY_OLD);
    CHECK(rtl_replay_check(&state, c2) == RTL_REPLAY_REPEAT);

    /* A subsequent group rekey consumes a strictly newer counter. */
    CHECK(rtl_replay_check(&state, c3) == RTL_REPLAY_NEW);
    rtl_replay_accept(&state, c3); /* Group M1 accepted; Group M1 repeat is rejected. */
    CHECK(rtl_replay_check(&state, c3) == RTL_REPLAY_REPEAT);
    CHECK(rtl_replay_check(&state, c2) == RTL_REPLAY_OLD);
    CHECK(rtl_replay_check(&state, c4) == RTL_REPLAY_NEW);

    /* Compare all 8 bytes, including a carry across the low 32-bit boundary. */
    rtl_replay_reset(&state);
    rtl_replay_accept(&state, pre32);
    CHECK(rtl_replay_check(&state, post32) == RTL_REPLAY_NEW);
    rtl_replay_accept(&state, post32);
    CHECK(rtl_replay_check(&state, pre32) == RTL_REPLAY_OLD);
    CHECK(rtl_replay_check(&state, high32) == RTL_REPLAY_NEW);

    /* A fresh M1 starts a new exchange and resets the base below prior state. */
    rtl_replay_begin(&state, high32); /* prior handshake */
    rtl_replay_begin(&state, c0); /* M1 unconditionally resets the handshake base. */
    CHECK(rtl_replay_check(&state, c0) == RTL_REPLAY_REPEAT);
    CHECK(rtl_replay_check(&state, c1) == RTL_REPLAY_NEW);

    if (failures) {
        fprintf(stderr, "%d replay test(s) failed\n", failures);
        return 1;
    }
    puts("replay tests passed");
    return 0;
}
