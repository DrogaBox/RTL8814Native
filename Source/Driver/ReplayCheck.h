#ifndef RTL8814_REPLAY_CHECK_H
#define RTL8814_REPLAY_CHECK_H

#include <stdint.h>

enum rtl_replay_result {
    RTL_REPLAY_OLD = -1,
    RTL_REPLAY_REPEAT = 0,
    RTL_REPLAY_NEW = 1
};

struct rtl_replay_state {
    uint8_t last[8];
    int valid;
};

/* EAPOL replay counters are unsigned 64-bit values in network byte order. */
enum rtl_replay_result rtl_replay_check(const struct rtl_replay_state *state,
                                        const uint8_t counter[8]);
void rtl_replay_accept(struct rtl_replay_state *state, const uint8_t counter[8]);
void rtl_replay_begin(struct rtl_replay_state *state, const uint8_t counter[8]);
void rtl_replay_reset(struct rtl_replay_state *state);

#endif
