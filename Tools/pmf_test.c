/* Userland checks for Source/Driver/PMF.c against Tools/pmf_vectors.h */
#include <stdio.h>
#include <string.h>
#include "PMF.h"
#include "pmf_vectors.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    unsigned char f[256];
    size_t out = 0;
    uint64_t pn = 0;
    unsigned int kid = 0;

    memcpy(f, pmf_deauth_enc, sizeof(pmf_deauth_enc));
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_enc), &out, &pn) == 0);
    CHECK(out == sizeof(pmf_deauth_plain));
    CHECK(memcmp(f, pmf_deauth_plain, sizeof(pmf_deauth_plain)) == 0);
    CHECK(pn == 0x0102030405a6ULL);

    memcpy(f, pmf_deauth_enc_retry, sizeof(pmf_deauth_enc_retry));
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_enc_retry), &out, &pn) == 0);
    CHECK(pn == 0x99ULL);

    memcpy(f, pmf_action_enc, sizeof(pmf_action_enc));
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_action_enc), &out, &pn) == 0);
    CHECK(out == sizeof(pmf_action_plain));
    CHECK(memcmp(f, pmf_action_plain, sizeof(pmf_action_plain)) == 0);

    memcpy(f, pmf_deauth_enc, sizeof(pmf_deauth_enc));
    f[sizeof(pmf_deauth_enc) - 1] ^= 1;
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_enc), &out, &pn) != 0);
    memcpy(f, pmf_deauth_enc, sizeof(pmf_deauth_enc));
    f[24] ^= 1;
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_enc), &out, &pn) != 0);
    memcpy(f, pmf_deauth_enc, sizeof(pmf_deauth_enc));
    f[4] ^= 1;
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_enc), &out, &pn) != 0);
    memcpy(f, pmf_deauth_enc, sizeof(pmf_deauth_enc));
    CHECK(rtl_pmf_ccmp_decrypt_mgmt(pmf_tk, f, 30, &out, &pn) != 0);

    /* Encrypting the plain vectors must give exactly the independent ciphertexts. */
    memset(f, 0, sizeof(f));
    memcpy(f, pmf_deauth_plain, sizeof(pmf_deauth_plain));
    CHECK(rtl_pmf_ccmp_encrypt_mgmt(pmf_tk, f, sizeof(pmf_deauth_plain), 0x0102030405a6ULL, &out) == 0);
    CHECK(out == sizeof(pmf_deauth_enc));
    CHECK(memcmp(f, pmf_deauth_enc, sizeof(pmf_deauth_enc)) == 0);
    memset(f, 0, sizeof(f));
    memcpy(f, pmf_action_plain, sizeof(pmf_action_plain));
    CHECK(rtl_pmf_ccmp_encrypt_mgmt(pmf_tk, f, sizeof(pmf_action_plain), 0xffULL, &out) == 0);
    CHECK(out == sizeof(pmf_action_enc));
    CHECK(memcmp(f, pmf_action_enc, sizeof(pmf_action_enc)) == 0);
    CHECK(rtl_pmf_ccmp_encrypt_mgmt(pmf_tk, f, sizeof(pmf_action_plain), 0, &out) != 0);
    CHECK(rtl_pmf_ccmp_encrypt_mgmt(pmf_tk, f, sizeof(pmf_action_plain), 1ULL << 48, &out) != 0);

    CHECK(rtl_pmf_bip_verify(pmf_igtk, pmf_bip, sizeof(pmf_bip), &kid, &pn) == 0);
    CHECK(kid == 4 && pn == 0x0a0b0cULL);
    memcpy(f, pmf_bip, sizeof(pmf_bip));
    f[sizeof(pmf_bip) - 1] ^= 1;
    CHECK(rtl_pmf_bip_verify(pmf_igtk, f, sizeof(pmf_bip), &kid, &pn) != 0);
    memcpy(f, pmf_bip, sizeof(pmf_bip));
    f[24] ^= 1;
    CHECK(rtl_pmf_bip_verify(pmf_igtk, f, sizeof(pmf_bip), &kid, &pn) != 0);
    CHECK(rtl_pmf_bip_verify(pmf_igtk, pmf_deauth_plain, sizeof(pmf_deauth_plain), &kid, &pn) != 0);

    printf(fails ? "pmf_test: %d FAILED\n" : "pmf_test: all passed\n", fails);
    return fails != 0;
}
