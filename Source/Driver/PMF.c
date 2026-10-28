/*
 * RTL8814Native — protected management frames (IEEE 802.11w), OS-free part.
 * See PMF.h.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#include "PMF.h"
#include "SAE.h"

#define PMF_FC_PROTECTED 0x40U /* bit 14 of the frame control, byte 1 */

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

/* CCM with L = 2 and an 8-byte tag (IEEE 802.11 12.5.3). */
int rtl_pmf_ccmp_decrypt_mgmt(const uint8_t tk[16], uint8_t *frame, size_t len,
                              size_t *out_len, uint64_t *pn)
{
    uint8_t nonce[13], aad[24], b[16], x[16], ctr[16], s[16], mic[8], tmp[16];
    uint8_t *ccmp, *data;
    size_t plen, off, i, n, aad_len;
    uint32_t counter;

    if (!tk || !frame || len < RTL_PMF_MGMT_HDR_LEN + RTL_PMF_CCMP_HDR_LEN + RTL_PMF_CCMP_MIC_LEN)
        return -1;
    if (!(frame[1] & PMF_FC_PROTECTED))
        return -1;
    ccmp = frame + RTL_PMF_MGMT_HDR_LEN;
    if (!(ccmp[3] & 0x20U)) /* Ext IV must be set for CCMP */
        return -1;

    plen = len - RTL_PMF_MGMT_HDR_LEN - RTL_PMF_CCMP_HDR_LEN - RTL_PMF_CCMP_MIC_LEN;
    data = ccmp + RTL_PMF_CCMP_HDR_LEN;

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
    aad_len = 22;

    /* Counter blocks: A0 for the tag, A1.. for the data. */
    ctr[0] = 0x01U;
    for (i = 0; i < 13; i++)
        ctr[1 + i] = nonce[i];

    /* Authentication: CBC-MAC over B0 || len(aad)+aad (padded) || plaintext (padded). */
    b[0] = (uint8_t)(0x40U | (((8U - 2U) / 2U) << 3) | 0x01U);
    for (i = 0; i < 13; i++)
        b[1 + i] = nonce[i];
    b[14] = (uint8_t)(plen >> 8);
    b[15] = (uint8_t)plen;
    rtl_aes128_encrypt_block(tk, b, x);

    /* AAD blocks: two-byte length || first 14 bytes, then the last 8 bytes padded. */
    for (i = 0; i < 16; i++)
        b[i] = 0;
    b[0] = (uint8_t)(aad_len >> 8);
    b[1] = (uint8_t)aad_len;
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

    /* Decrypt in place, feeding the plaintext to the CBC-MAC block by block. */
    for (off = 0, counter = 1; off < plen; off += 16, counter++) {
        ctr[14] = (uint8_t)(counter >> 8);
        ctr[15] = (uint8_t)counter;
        rtl_aes128_encrypt_block(tk, ctr, s);
        n = plen - off < 16 ? plen - off : 16;
        for (i = 0; i < n; i++)
            data[off + i] = (uint8_t)(data[off + i] ^ s[i]);
        for (i = 0; i < 16; i++)
            x[i] = (uint8_t)(x[i] ^ (i < n ? data[off + i] : 0));
        rtl_aes128_encrypt_block(tk, x, tmp);
        for (i = 0; i < 16; i++)
            x[i] = tmp[i];
    }

    ctr[14] = 0;
    ctr[15] = 0;
    rtl_aes128_encrypt_block(tk, ctr, s);
    for (i = 0; i < 8; i++)
        mic[i] = (uint8_t)(x[i] ^ s[i]);
    if (!pmf_equal(mic, data + plen, 8)) {
        /* The plaintext is garbage on failure: wipe it, the caller drops the frame. */
        pmf_wipe(data, plen);
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
    if (!pmf_equal(mac, mme + 10, 8))
        return -1;
    if (key_id)
        *key_id = (unsigned int)(mme[2] | (mme[3] << 8));
    if (ipn) {
        uint64_t v = 0;
        for (i = 0; i < 6; i++)
            v |= (uint64_t)mme[4 + i] << (8 * i);
        *ipn = v;
    }
    return 0;
}
