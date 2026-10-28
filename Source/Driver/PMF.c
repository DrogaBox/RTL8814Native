/*
 * RTL8814Native — protected management frames (IEEE 802.11w), OS-free part.
 * See PMF.h.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "PMF.h"
#include "SAE.h"

#define PMF_FC_PROTECTED 0x40U /* bit 14 of the frame control, byte 1 */

int rtl_pmf_management_body_valid(uint8_t subtype, size_t len)
{
    if (len < RTL_PMF_MGMT_HDR_LEN)
        return 0;
    if (subtype == 10 || subtype == 12)
        return len >= RTL_PMF_MGMT_HDR_LEN + 2U;
    if (subtype == 13)
        return len >= RTL_PMF_MGMT_HDR_LEN + 2U;
    return 1;
}

int rtl_pmf_unprotected_action_allowed(const uint8_t *frame, size_t len)
{
    if (!frame || len < RTL_PMF_MGMT_HDR_LEN + 2U)
        return 0;
    return !rtl_pmf_action_requires_protection(frame[RTL_PMF_MGMT_HDR_LEN]);
}

int rtl_pmf_bip_body_valid(uint8_t subtype, const uint8_t *frame, size_t len)
{
    if (!frame || len < RTL_PMF_MGMT_HDR_LEN)
        return 0;
    if (subtype == 10 || subtype == 12)
        return len >= RTL_PMF_MGMT_HDR_LEN + 2U + RTL_PMF_MME_LEN;
    if (subtype == 13) {
        size_t body_len;
        if (len < RTL_PMF_MGMT_HDR_LEN + RTL_PMF_MME_LEN)
            return 0;
        body_len = len - RTL_PMF_MGMT_HDR_LEN - RTL_PMF_MME_LEN;
        return body_len >= 2U &&
               rtl_pmf_action_requires_protection(frame[RTL_PMF_MGMT_HDR_LEN]);
    }
    return 1;
}

int rtl_pmf_action_requires_protection(uint8_t category)
{
    switch (category) {
    case 4:  /* Public */
    case 7:  /* HT */
    case 11: /* Unprotected WNM */
    case 15: /* Self-protected */
    case 21: /* VHT */
    case 127: /* Vendor-specific P2P */
        return 0;
    default:
        return 1;
    }
}

static int pmf_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    size_t i;
    for (i = 0; i < n; i++)
        diff = (uint8_t)(diff | (a[i] ^ b[i]));
    return diff == 0;
}

static void pmf_wipe(uint8_t *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--)
        *v++ = 0;
}

/*
 * CCM with L = 2 and an 8-byte tag (IEEE 802.11 12.5.3). The nonce and the AAD
 * come from the header and the CCMP header already in the frame; x ends up as
 * the CBC-MAC state after B0 and the two AAD blocks, ctr as counter block A0.
 */
static void pmf_ccm_setup(const uint8_t tk[16], const uint8_t *frame, size_t plen,
                          uint8_t x[16], uint8_t ctr[16])
{
    const uint8_t *ccmp = frame + RTL_PMF_MGMT_HDR_LEN;
    uint8_t nonce[13], aad[22], b[16], tmp[16];
    size_t i;

    /* Nonce = flags || A2 || PN (PN5..PN0). flags: priority 0, Management bit set. */
    nonce[0] = 0x10U;
    for (i = 0; i < 6; i++)
        nonce[1 + i] = frame[10 + i];
    nonce[7] = ccmp[7]; nonce[8] = ccmp[6]; nonce[9] = ccmp[5];
    nonce[10] = ccmp[4]; nonce[11] = ccmp[1]; nonce[12] = ccmp[0];

    /* AAD = FC (retry, power management, more data cleared) || A1 || A2 || A3 || SC (fragment only) */
    aad[0] = frame[0];
    aad[1] = (uint8_t)(frame[1] & ~(0x08U | 0x10U | 0x20U));
    for (i = 0; i < 18; i++)
        aad[2 + i] = frame[4 + i];
    aad[20] = (uint8_t)(frame[22] & 0x0FU);
    aad[21] = 0;

    ctr[0] = 0x01U;
    for (i = 0; i < 13; i++)
        ctr[1 + i] = nonce[i];
    ctr[14] = 0;
    ctr[15] = 0;

    /* B0, then the AAD: two-byte length || first 14 bytes, then the last 8 padded. */
    b[0] = (uint8_t)(0x40U | (((8U - 2U) / 2U) << 3) | 0x01U);
    for (i = 0; i < 13; i++)
        b[1 + i] = nonce[i];
    b[14] = (uint8_t)(plen >> 8);
    b[15] = (uint8_t)plen;
    rtl_aes128_encrypt_block(tk, b, x);

    for (i = 0; i < 16; i++)
        b[i] = 0;
    b[0] = 0;
    b[1] = (uint8_t)sizeof(aad);
    for (i = 0; i < 14; i++)
        b[2 + i] = aad[i];
    for (i = 0; i < 16; i++)
        x[i] = (uint8_t)(x[i] ^ b[i]);
    rtl_aes128_encrypt_block(tk, x, tmp);
    for (i = 0; i < 16; i++)
        b[i] = 0;
    for (i = 0; i < 8; i++)
        b[i] = aad[14 + i];
    for (i = 0; i < 16; i++)
        x[i] = (uint8_t)(tmp[i] ^ b[i]);
    rtl_aes128_encrypt_block(tk, x, tmp);
    for (i = 0; i < 16; i++)
        x[i] = tmp[i];
    pmf_wipe(nonce, sizeof(nonce));
    pmf_wipe(aad, sizeof(aad));
    pmf_wipe(b, sizeof(b));
    pmf_wipe(tmp, sizeof(tmp));
}

/* CBC-MAC over data (plaintext), then the tag = MAC ^ E(A0). */
static void pmf_ccm_tag(const uint8_t tk[16], uint8_t x[16], uint8_t ctr[16],
                        const uint8_t *plain, size_t plen, uint8_t mic[8])
{
    uint8_t tmp[16], s[16];
    size_t off, n, i;

    for (off = 0; off < plen; off += 16) {
        n = plen - off < 16 ? plen - off : 16;
        for (i = 0; i < 16; i++)
            x[i] = (uint8_t)(x[i] ^ (i < n ? plain[off + i] : 0));
        rtl_aes128_encrypt_block(tk, x, tmp);
        for (i = 0; i < 16; i++)
            x[i] = tmp[i];
    }
    ctr[14] = 0;
    ctr[15] = 0;
    rtl_aes128_encrypt_block(tk, ctr, s);
    for (i = 0; i < 8; i++)
        mic[i] = (uint8_t)(x[i] ^ s[i]);
    pmf_wipe(tmp, sizeof(tmp));
    pmf_wipe(s, sizeof(s));
}

/* CTR keystream over data in place, counters 1... */
static void pmf_ccm_ctr(const uint8_t tk[16], uint8_t ctr[16], uint8_t *data, size_t plen)
{
    uint8_t s[16];
    size_t off, n, i;
    uint32_t counter;

    for (off = 0, counter = 1; off < plen; off += 16, counter++) {
        ctr[14] = (uint8_t)(counter >> 8);
        ctr[15] = (uint8_t)counter;
        rtl_aes128_encrypt_block(tk, ctr, s);
        n = plen - off < 16 ? plen - off : 16;
        for (i = 0; i < n; i++)
            data[off + i] = (uint8_t)(data[off + i] ^ s[i]);
    }
    pmf_wipe(s, sizeof(s));
}

int rtl_pmf_ccmp_decrypt_mgmt(const uint8_t tk[16], uint8_t *frame, size_t len,
                              size_t *out_len, uint64_t *pn)
{
    uint8_t x[16], ctr[16], mic[8];
    uint8_t *ccmp, *data;
    size_t plen, i;

    if (!tk || !frame || len < RTL_PMF_MGMT_HDR_LEN + RTL_PMF_CCMP_HDR_LEN + RTL_PMF_CCMP_MIC_LEN)
        return -1;
    if (!(frame[1] & PMF_FC_PROTECTED))
        return -1;
    ccmp = frame + RTL_PMF_MGMT_HDR_LEN;
    if (!(ccmp[3] & 0x20U)) /* Ext IV must be set for CCMP */
        return -1;

    plen = len - RTL_PMF_MGMT_HDR_LEN - RTL_PMF_CCMP_HDR_LEN - RTL_PMF_CCMP_MIC_LEN;
    data = ccmp + RTL_PMF_CCMP_HDR_LEN;

    pmf_ccm_setup(tk, frame, plen, x, ctr);
    pmf_ccm_ctr(tk, ctr, data, plen);
    pmf_ccm_tag(tk, x, ctr, data, plen, mic);
    if (!pmf_equal(mic, data + plen, 8)) {
        /* The plaintext is garbage on failure: wipe it, the caller drops the frame. */
        pmf_wipe(data, plen);
        pmf_wipe(x, sizeof(x));
        pmf_wipe(ctr, sizeof(ctr));
        pmf_wipe(mic, sizeof(mic));
        return -1;
    }

    if (pn) {
        *pn = ((uint64_t)ccmp[7] << 40) | ((uint64_t)ccmp[6] << 32) |
              ((uint64_t)ccmp[5] << 24) | ((uint64_t)ccmp[4] << 16) |
              ((uint64_t)ccmp[1] << 8) | (uint64_t)ccmp[0];
    }
    for (i = 0; i < plen; i++)
        ccmp[i] = data[i];
    frame[1] = (uint8_t)(frame[1] & ~PMF_FC_PROTECTED);
    if (out_len)
        *out_len = RTL_PMF_MGMT_HDR_LEN + plen;
    pmf_wipe(x, sizeof(x));
    pmf_wipe(ctr, sizeof(ctr));
    pmf_wipe(mic, sizeof(mic));
    return 0;
}

int rtl_pmf_ccmp_encrypt_mgmt(const uint8_t tk[16], uint8_t *frame, size_t len,
                              uint64_t pn, size_t *out_len)
{
    uint8_t x[16], ctr[16], mic[8];
    uint8_t *ccmp, *data;
    size_t plen, i;

    if (!tk || !frame || len < RTL_PMF_MGMT_HDR_LEN || pn == 0 || pn >> 48)
        return -1;
    plen = len - RTL_PMF_MGMT_HDR_LEN;
    ccmp = frame + RTL_PMF_MGMT_HDR_LEN;
    data = ccmp + RTL_PMF_CCMP_HDR_LEN;

    for (i = plen; i-- > 0;)
        data[i] = ccmp[i];
    ccmp[0] = (uint8_t)pn;
    ccmp[1] = (uint8_t)(pn >> 8);
    ccmp[2] = 0;
    ccmp[3] = 0x20U; /* Ext IV, key id 0 */
    ccmp[4] = (uint8_t)(pn >> 16);
    ccmp[5] = (uint8_t)(pn >> 24);
    ccmp[6] = (uint8_t)(pn >> 32);
    ccmp[7] = (uint8_t)(pn >> 40);
    frame[1] = (uint8_t)(frame[1] | PMF_FC_PROTECTED);

    pmf_ccm_setup(tk, frame, plen, x, ctr);
    pmf_ccm_tag(tk, x, ctr, data, plen, mic);
    pmf_ccm_ctr(tk, ctr, data, plen);
    for (i = 0; i < 8; i++)
        data[plen + i] = mic[i];
    if (out_len)
        *out_len = RTL_PMF_MGMT_HDR_LEN + RTL_PMF_CCMP_HDR_LEN + plen + RTL_PMF_CCMP_MIC_LEN;
    pmf_wipe(x, sizeof(x));
    pmf_wipe(ctr, sizeof(ctr));
    pmf_wipe(mic, sizeof(mic));
    return 0;
}

int rtl_pmf_bip_verify(const uint8_t igtk[16], const uint8_t *frame, size_t len,
                       unsigned int *key_id, uint64_t *ipn)
{
    uint8_t buf[512], mac[16];
    const uint8_t *mme;
    size_t i;

    if (!igtk || !frame || len < RTL_PMF_MGMT_HDR_LEN + RTL_PMF_MME_LEN || len > 480U)
        return -1;
    mme = frame + len - RTL_PMF_MME_LEN;
    if (mme[0] != 76U || mme[1] != 16U)
        return -1;

    /* AAD (FC with retry/power management/more data cleared || A1 || A2 || A3), then the body with a zeroed MIC. */
    buf[0] = frame[0];
    buf[1] = (uint8_t)(frame[1] & ~(0x08U | 0x10U | 0x20U));
    for (i = 0; i < 18; i++)
        buf[2 + i] = frame[4 + i];
    for (i = 0; i < len - RTL_PMF_MGMT_HDR_LEN; i++)
        buf[20 + i] = frame[RTL_PMF_MGMT_HDR_LEN + i];
    for (i = 0; i < 8; i++)
        buf[20 + (len - RTL_PMF_MGMT_HDR_LEN) - 8 + i] = 0;
    rtl_aes128_cmac(igtk, buf, 20 + (len - RTL_PMF_MGMT_HDR_LEN), mac);
    if (!pmf_equal(mac, mme + 10, 8)) {
        pmf_wipe(buf, sizeof(buf));
        pmf_wipe(mac, sizeof(mac));
        return -1;
    }
    if (key_id)
        *key_id = (unsigned int)(mme[2] | (mme[3] << 8));
    if (ipn) {
        uint64_t v = 0;
        for (i = 0; i < 6; i++)
            v |= (uint64_t)mme[4 + i] << (8 * i);
        *ipn = v;
    }
    pmf_wipe(buf, sizeof(buf));
    pmf_wipe(mac, sizeof(mac));
    return 0;
}
