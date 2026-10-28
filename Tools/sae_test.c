/*
 * RTL8814Native — standalone SAE vector harness
 *
 * Build and run with:
 *   mkdir -p Build
 *   clang -DRTL_SAE_TESTING -std=c11 -Wall -Wextra -Werror -O2 -ISource/Driver \
 *       Source/Driver/SAE.c Tools/sae_test.c -o Build/sae_test && ./Build/sae_test
 *
 * Fixed values are from hostap/wpa_supplicant's common_module_tests.c,
 * which labels this case IEEE P802.11-REVmd/D2.1 Annex J.10.
 */
#include "SAE.h"

#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const uint8_t addr1[6] = {0x82, 0x7b, 0x91, 0x9d, 0xd4, 0xb9};
static const uint8_t addr2[6] = {0x1e, 0xec, 0x49, 0xea, 0x64, 0x88};
static const uint8_t local_rand[32] = {
    0xa9,0x06,0xf6,0x1e,0x4d,0x3a,0x5d,0x4e,0xb2,0x96,0x5f,0xf3,0x4c,0xf9,0x17,0xdd,
    0x04,0x44,0x45,0xc8,0x78,0xc1,0x7c,0xa5,0xd5,0xb9,0x37,0x86,0xda,0x9f,0x83,0xcf
};
static const uint8_t local_mask[32] = {
    0x42,0x34,0xb4,0xfb,0x17,0xaa,0x43,0x5c,0x52,0xfb,0xfd,0xeb,0xe6,0x40,0x39,0xb4,
    0x34,0x78,0x20,0x0e,0x54,0xff,0x7b,0x6e,0x07,0xb6,0x9c,0xad,0x74,0x15,0x3c,0x15
};
static const uint8_t expected_commit[] = {
    0x13,0x00,0xeb,0x3b,0xab,0x19,0x64,0xe4,0xa0,0xab,0x05,0x92,0x5d,0xdf,0x33,0x39,
    0x51,0x91,0x38,0xbc,0x65,0xd6,0xcd,0xc0,0xf8,0x13,0xdd,0x6f,0xd4,0x34,0x4e,0xb4,
    0xbf,0xe4,0x4b,0x5c,0x21,0x59,0x76,0x58,0xf4,0xe3,0xed,0xdf,0xb4,0xb9,0x9f,0x25,
    0xb4,0xd6,0x54,0x0f,0x32,0xff,0x1f,0xd5,0xc5,0x30,0xc6,0x0a,0x79,0x44,0x48,0x61,
    0x0b,0xc6,0xde,0x3d,0x92,0xbd,0xbb,0xd4,0x7d,0x93,0x59,0x80,0xca,0x6c,0xf8,0x98,
    0x8a,0xb6,0x63,0x0b,0xe6,0x76,0x4c,0x88,0x5c,0xeb,0x97,0x93,0x97,0x0f,0x69,0x52,
    0x17,0xee,0xff,0x0d,0x21,0x70,0x73,0x6b,0x34,0x69,0x6e,0x74,0x65,0x72,0x6e,0x65,
    0x74
};
static const uint8_t peer_commit[] = {
    0x13,0x00,0x55,0x64,0xf0,0x45,0xb2,0xea,0x1e,0x56,0x6c,0xf1,0xdd,0x74,0x1f,0x70,
    0xd9,0xbe,0x35,0xd2,0xdf,0x5b,0x9a,0x55,0x02,0x94,0x6e,0xe0,0x3c,0xf8,0xda,0xe2,
    0x7e,0x1e,0x05,0xb8,0x43,0x0e,0xb7,0xa9,0x9e,0x24,0x87,0x7c,0xe6,0x9b,0xaf,0x3d,
    0xc5,0x80,0xe3,0x09,0x63,0x3d,0x6b,0x38,0x5f,0x83,0xee,0x1c,0x3e,0xc3,0x59,0x1f,
    0x1a,0x53,0x93,0xc0,0x6e,0x80,0x5d,0xdc,0xeb,0x2f,0xde,0x50,0x93,0x0d,0xd7,0xcf,
    0xeb,0xb9,0x87,0xc6,0xff,0x96,0x66,0xaf,0x16,0x4e,0xb5,0x18,0x4d,0x8e,0x66,0x62,
    0xed,0x6a,0xff,0x0d,0x21,0x70,0x73,0x6b,0x34,0x69,0x6e,0x74,0x65,0x72,0x6e,0x65,
    0x74
};
static const uint8_t expected_kck[32] = {
    0x59,0x9d,0x6f,0x1e,0x27,0x54,0x8b,0xe8,0x49,0x9d,0xce,0xed,0x2f,0xec,0xcf,0x94,
    0x81,0x8c,0xe1,0xc7,0x9f,0x1b,0x4e,0xb3,0xd6,0xa5,0x32,0x28,0xa0,0x9b,0xf3,0xed
};
static const uint8_t expected_pmk[32] = {
    0x7a,0xea,0xd8,0x6f,0xba,0x4c,0x32,0x21,0xfc,0x43,0x7f,0x5f,0x14,0xd7,0x0d,0x85,
    0x4e,0xa5,0xd5,0xaa,0xc1,0x69,0x01,0x16,0x79,0x30,0x81,0xed,0xa4,0xd5,0x57,0xc5
};
static const uint8_t expected_pmkid[16] = {
    0x40,0xa0,0x9b,0x60,0x17,0xce,0xbf,0x00,0x72,0x84,0x3b,0x53,0x52,0xaa,0x2b,0x4f
};
static const uint8_t expected_confirm[34] = {
    0x01,0x00,0x12,0xd9,0xd5,0xc7,0x8c,0x50,0x05,0x26,0xd3,0x6c,0x41,0xdb,0xc5,0x6a,
    0xed,0xf2,0x91,0x4c,0xed,0xdd,0xd7,0xca,0xd4,0xa5,0x8c,0x48,0xf8,0x3d,0xbd,0xe9,
    0xfc,0x77
};
static const uint8_t peer_confirm[34] = {
    0x01,0x00,0x02,0x87,0x1c,0xf9,0x06,0x89,0x8b,0x80,0x60,0xec,0x18,0x41,0x43,0xbe,
    0x77,0xb8,0xc0,0x8a,0x80,0x19,0xb1,0x3e,0xb6,0xd0,0xae,0xf0,0xd8,0x38,0x3d,0xfa,
    0xc2,0xfd
};

static int check(const char *name, const uint8_t *actual,
                 const uint8_t *expected, size_t length)
{
    if (memcmp(actual, expected, length) == 0) {
        printf("ok: %s (%zu bytes)\n", name, length);
        return 0;
    }
    fprintf(stderr, "FAIL: %s (%zu bytes)\n", name, length);
    return -1;
}

static int zeroed(const void *memory, size_t length)
{
    const uint8_t *p = memory;
    uint8_t value = 0;
    for (size_t i = 0; i < length; i++)
        value |= p[i];
    return value == 0;
}

static void wipe(void *memory, size_t length)
{
    volatile uint8_t *p = memory;
    while (length--)
        *p++ = 0;
}

struct test_rng {
    unsigned int calls;
    unsigned int fail;
};

static int test_random(void *context, uint8_t *output, size_t length)
{
    struct test_rng *rng = context;
    if (rng->fail)
        return -1;
    memset(output, 0x5a, length);
    if (rng->calls == 2 && length == sizeof(local_rand))
        memcpy(output, local_rand, length);
    if (rng->calls == 3 && length == sizeof(local_mask))
        memcpy(output, local_mask, length);
    rng->calls++;
    return 0;
}

#define REQUIRE(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); failed = 1; goto done; } \
    printf("ok: %s\n", message); \
} while (0)

int main(void)
{
    struct rtl_sae_p256 sae, peer;
    uint8_t commit[RTL_SAE_COMMIT_MAX_LEN];
    uint8_t confirm[RTL_SAE_CONFIRM_LEN];
    uint8_t pmk[RTL_SAE_PMK_LEN], pmkid[RTL_SAE_PMKID_LEN];
    uint8_t token[32], retry[RTL_SAE_COMMIT_MAX_LEN], bad[sizeof(peer_commit)];
    uint8_t bad_confirm[RTL_SAE_CONFIRM_LEN], peer_reply[RTL_SAE_CONFIRM_LEN];
    uint8_t peer_pmk[RTL_SAE_PMK_LEN], peer_pmkid[RTL_SAE_PMKID_LEN];
    size_t commit_len = 0, retry_len = 0;
    struct test_rng rng = {0, 0};
    int failed = 0;

    memset(&sae, 0, sizeof(sae));
    memset(&peer, 0, sizeof(peer));
    if (rtl_sae_p256_start_fixed(&sae,
                                 (const uint8_t *)"mekmitasdigoat", 14,
                                 addr1, addr2,
                                 (const uint8_t *)"psk4internet", 12,
                                 local_rand, local_mask) < 0) {
        fprintf(stderr, "FAIL: derive PWE and local commit\n");
        failed = 1;
        goto done;
    }
    REQUIRE(rtl_sae_p256_write_commit(&sae, commit, sizeof(commit), &commit_len) == 0,
            "write local commit");
    REQUIRE(commit_len == ARRAY_COUNT(expected_commit), "local commit length");
    failed |= check("Annex J.10 local commit", commit, expected_commit,
                    ARRAY_COUNT(expected_commit)) < 0;

    memset(token, 0xa5, sizeof(token));
    REQUIRE(rtl_sae_p256_set_token(&sae, token, sizeof(token)) == 0 &&
            rtl_sae_p256_write_commit(&sae, retry, sizeof(retry), &retry_len) == 0 &&
            retry_len == commit_len + sizeof(token) &&
            memcmp(retry, commit, 2) == 0 &&
            memcmp(retry + 2, token, sizeof(token)) == 0 &&
            memcmp(retry + 2 + sizeof(token), commit + 2, commit_len - 2) == 0,
            "anti-clogging retry preserves commit and includes token");
    REQUIRE(rtl_sae_p256_set_token(&sae, NULL, 0) < 0, "reject empty token");

    REQUIRE(rtl_sae_p256_process_commit(&sae, peer_commit, sizeof(peer_commit), 0) == 0,
            "process Annex J.10 peer commit");
    failed |= check("Annex J.10 KCK", sae.kck, expected_kck, sizeof(expected_kck)) < 0;
    failed |= check("Annex J.10 PMK", sae.pmk, expected_pmk, sizeof(expected_pmk)) < 0;
    failed |= check("Annex J.10 PMKID", sae.pmkid, expected_pmkid, sizeof(expected_pmkid)) < 0;
    REQUIRE(rtl_sae_p256_finish(&sae, pmk, pmkid) < 0 &&
            zeroed(pmk, sizeof(pmk)) && zeroed(pmkid, sizeof(pmkid)),
            "refuse keys before confirm and clear outputs");
    REQUIRE(rtl_sae_p256_write_confirm(&sae, confirm) == 0,
            "write initial confirm");
    failed |= check("Annex J.10 local confirm", confirm, expected_confirm,
                    sizeof(expected_confirm)) < 0;
    memcpy(bad_confirm, peer_confirm, sizeof(bad_confirm));
    bad_confirm[4] ^= 1;
    REQUIRE(rtl_sae_p256_check_confirm(&sae, bad_confirm, sizeof(bad_confirm)) < 0,
            "reject modified confirm");
    if (rtl_sae_p256_check_confirm(&sae, peer_confirm, sizeof(peer_confirm)) < 0) {
        fprintf(stderr, "FAIL: verify Annex J.10 peer confirm\n");
        failed = 1;
    } else {
        puts("ok: Annex J.10 peer confirm");
    }
    REQUIRE(rtl_sae_p256_check_confirm(&sae, peer_confirm, sizeof(peer_confirm)) < 0,
            "reject replayed peer confirm");
    REQUIRE(rtl_sae_p256_write_confirm(&sae, confirm) == 0 &&
            confirm[0] == 2 && confirm[1] == 0,
            "advance send-confirm counter");
    sae.send_confirm = UINT16_MAX;
    REQUIRE(rtl_sae_p256_write_confirm(&sae, confirm) == 0 &&
            confirm[0] == 0xff && confirm[1] == 0xff &&
            sae.send_confirm == UINT16_MAX,
            "saturate send-confirm counter");
    if (rtl_sae_p256_finish(&sae, pmk, pmkid) < 0) {
        fprintf(stderr, "FAIL: finish exchange and export PMK/PMKID\n");
        failed = 1;
    } else {
        failed |= check("exported PMK", pmk, expected_pmk, sizeof(expected_pmk)) < 0;
        failed |= check("exported PMKID", pmkid, expected_pmkid, sizeof(expected_pmkid)) < 0;
    }
    REQUIRE(zeroed(&sae, sizeof(sae)), "finish wipes exchange state");
    wipe(pmk, sizeof(pmk));
    wipe(pmkid, sizeof(pmkid));
    REQUIRE(rtl_sae_p256_start(&sae, (const uint8_t *)"mekmitasdigoat", 14,
                               addr1, addr2, (const uint8_t *)"psk4internet", 12,
                               test_random, &rng) == 0 && rng.calls == 4,
            "start with supplied entropy");
    REQUIRE(rtl_sae_p256_write_commit(&sae, commit, sizeof(commit), &commit_len) == 0 &&
            commit_len == sizeof(expected_commit) &&
            memcmp(commit, expected_commit, commit_len) == 0,
            "entropy-based commit matches fixed vector");
    REQUIRE(rtl_sae_p256_process_commit(&sae, commit, commit_len, 0) < 0 &&
            zeroed(&sae, sizeof(sae)), "reject reflected commit and wipe state");

    rng.calls = 0;
    REQUIRE(rtl_sae_p256_start(&sae, (const uint8_t *)"mekmitasdigoat", 14,
                               addr1, addr2, (const uint8_t *)"psk4internet", 12,
                               test_random, &rng) == 0, "restart test exchange");
    memcpy(bad, peer_commit, sizeof(bad));
    memset(bad + 34, 0xff, 32);
    REQUIRE(rtl_sae_p256_process_commit(&sae, bad, sizeof(bad), 0) < 0 &&
            zeroed(&sae, sizeof(sae)), "reject invalid peer point and wipe state");
    rng.fail = 1;
    REQUIRE(rtl_sae_p256_start(&sae, (const uint8_t *)"mekmitasdigoat", 14,
                               addr1, addr2, NULL, 0, test_random, &rng) < 0 &&
            zeroed(&sae, sizeof(sae)), "reject entropy failure and wipe state");

    REQUIRE(rtl_sae_p256_start_fixed(&sae, (const uint8_t *)"mekmitasdigoat", 14,
                                     addr1, addr2, NULL, 0, local_rand, local_mask) == 0 &&
            rtl_sae_p256_start_fixed(&peer, (const uint8_t *)"mekmitasdigoat", 14,
                                     addr2, addr1, NULL, 0, local_mask, local_rand) == 0,
            "derive password elements without identifiers");
    REQUIRE(rtl_sae_p256_write_commit(&sae, commit, sizeof(commit), &commit_len) == 0 &&
            rtl_sae_p256_write_commit(&peer, retry, sizeof(retry), &retry_len) == 0 &&
            rtl_sae_p256_process_commit(&sae, retry, retry_len, 0) == 0 &&
            rtl_sae_p256_process_commit(&peer, commit, commit_len, 0) == 0,
            "exchange peer commits without identifiers");
    REQUIRE(rtl_sae_p256_write_confirm(&sae, confirm) == 0 &&
            rtl_sae_p256_write_confirm(&peer, peer_reply) == 0 &&
            rtl_sae_p256_check_confirm(&sae, peer_reply, sizeof(peer_reply)) == 0 &&
            rtl_sae_p256_check_confirm(&peer, confirm, sizeof(confirm)) == 0 &&
            rtl_sae_p256_finish(&sae, pmk, pmkid) == 0 &&
            rtl_sae_p256_finish(&peer, peer_pmk, peer_pmkid) == 0 &&
            memcmp(pmk, peer_pmk, sizeof(pmk)) == 0 &&
            memcmp(pmkid, peer_pmkid, sizeof(pmkid)) == 0 &&
            zeroed(&sae, sizeof(sae)) && zeroed(&peer, sizeof(peer)),
            "confirm both sides, agree on keys, and wipe state");

done:
    rtl_sae_p256_clear(&sae);
    rtl_sae_p256_clear(&peer);
    wipe(pmk, sizeof(pmk));
    wipe(pmkid, sizeof(pmkid));
    wipe(peer_pmk, sizeof(peer_pmk));
    wipe(peer_pmkid, sizeof(peer_pmkid));
    wipe(confirm, sizeof(confirm));
    wipe(peer_reply, sizeof(peer_reply));
    wipe(bad_confirm, sizeof(bad_confirm));
    return failed ? 1 : 0;
}
