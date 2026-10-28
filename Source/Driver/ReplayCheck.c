#include "ReplayCheck.h"

#include <string.h>

enum rtl_replay_result rtl_replay_check(const struct rtl_replay_state *state,
                                        const uint8_t counter[8])
{
    int greater = 0;
    int less = 0;
    unsigned int i;

    if (!state || !counter || !state->valid)
        return RTL_REPLAY_NEW;

    for (i = 0; i < 8; i++) {
        unsigned int undecided = (unsigned int)(!(greater | less));
        greater |= (int)(undecided & (unsigned int)(counter[i] > state->last[i]));
        less |= (int)(undecided & (unsigned int)(counter[i] < state->last[i]));
    }
    if (greater)
        return RTL_REPLAY_NEW;
    if (less)
        return RTL_REPLAY_OLD;
    return RTL_REPLAY_REPEAT;
}

void rtl_replay_accept(struct rtl_replay_state *state, const uint8_t counter[8])
{
    if (!state || !counter)
        return;
    memcpy(state->last, counter, sizeof(state->last));
    state->valid = 1;
}

void rtl_replay_reset(struct rtl_replay_state *state)
{
    if (!state)
        return;
    memset(state, 0, sizeof(*state));
}

void rtl_replay_begin(struct rtl_replay_state *state, const uint8_t counter[8])
{
    rtl_replay_reset(state);
    rtl_replay_accept(state, counter);
}
