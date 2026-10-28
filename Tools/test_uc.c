/*
 * RTL8814Native — Userspace Diagnostic, Telemetry, Scan & WPA2 Join CLI
 *
 * Usage:
 *   ./Tools/test_uc                     (Show Radio Telemetry + Link Status)
 *   ./Tools/test_uc status              (Show Radio Telemetry + Link Status)
 *   ./Tools/test_uc scan                (Trigger 802.11 Scan & list networks)
 *   ./Tools/test_uc join <SSID> [PSK]   (Associate & run WPA2-PSK 4-Way Handshake)
 *   ./Tools/test_uc disconnect          (Disconnect from current network)
 *   ./Tools/test_uc peek <addr> [w]     (Read MAC/BB register, width 1/2/4, default 4)
 *   ./Tools/test_uc poke <addr> <val> [w] (Write MAC/BB register)
 *   ./Tools/test_uc rf <path> <reg>     (Read RF register, path 0..3 = A..D)
 *   ./Tools/test_uc rxlog               (Dump the last 16 raw bulk IN transfers)
 *   ./Tools/test_uc reassemble <0|1>    (Toggle carrying split frames across transfers)
 *   ./Tools/test_uc recycle <0|1>       (Toggle reusing bulk IN skbs instead of a 32KB alloc per URB)
 *   ./Tools/test_uc perfreset           (Zero RX aggregation/timing counters)
 *
 * Build:
 *   clang -Wall -O2 Tools/test_uc.c -framework IOKit -framework CoreFoundation -o Tools/test_uc
 */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include "../Source/Driver/RTLRadioABI.h"
#include "../Source/Driver/RTLJoinLog.h"

enum {
    kRTLUC_GetVersion    = 0,
    kRTLUC_GetLinkStatus = 1,
    kRTLUC_TriggerScan   = 2,
    kRTLUC_GetScanResult = 3,
    kRTLUC_JoinNetwork   = 4,
    kRTLUC_Disconnect    = 5,
    kRTLUC_GetRadioInfo  = 6,
    kRTLUC_RegAccess     = 7,
    kRTLUC_GetRxLog      = 8,
    kRTLUC_RadioCall     = 9,
    kRTLUC_GetJoinLog    = 10,
};

#define RX_XFER_LOG_N 16
struct rtl8814_rx_xfer {
    uint32_t seq;
    uint32_t len;
    uint8_t  head[32];
    uint8_t  tail[32];
};
struct rtl8814_rx_log {
    uint32_t next_seq;
    struct rtl8814_rx_xfer xfer[RX_XFER_LOG_N];
};

#define RTL_MAX_SCAN_ENTRIES 32

struct rtl_scan_entry {
    char     ssid[33];
    uint8_t  bssid[6];
    int16_t  rssi;
    uint8_t  channel;
    uint8_t  security; /* 0=Open, 1=WEP, 2=WPA, 3=WPA2 */
};

struct rtl8814_trx_diag {
    uint32_t urb_bad_len;
    uint32_t last_bad_urb_len;
    uint32_t last_urb_len;
    uint32_t drop_no_recvframe;
    uint32_t drop_crc;
    uint32_t drop_icv;
    uint32_t drop_bad_pktlen;
    uint32_t drop_runt;
    uint32_t drop_alloc;
    uint32_t rx_normal;
    uint32_t rx_c2h;
    uint32_t rx_other_rpt;
    uint32_t rx_mgmt;
    uint32_t rx_beacon;
    uint32_t rx_probe_rsp;
    uint32_t rx_ctrl;
    uint32_t rx_data;
    uint8_t  last_desc[24];
    uint32_t tx_write_ok;
    uint32_t tx_write_err;
    int32_t  tx_last_err;
    uint32_t tx_last_ep;
    uint32_t rx_carry;
    uint32_t rx_reassembled;
    uint32_t rx_stage_dropped;
};

struct rtl8814_rate_diag {
    uint8_t  cur_bw;
    uint8_t  cur_ch_offset;
    uint8_t  wireless_mode;
    uint8_t  ht_option;
    uint8_t  vht_option;
    uint8_t  vht_sgi_80m;
    uint8_t  vht_highest_rate;
    uint8_t  vht_ampdu_len;
    uint8_t  sta_bw;
    uint8_t  ra_bw;
    uint8_t  tx_rate;
    uint8_t  tx_sgi;
    uint8_t  tx_bw;
    uint8_t  rx_rate;
    uint8_t  hal_tx_nss;
    uint8_t  hal_rx_nss;
    uint8_t  ba_rx_tids;
    uint8_t  ba_rx_wsize;
    uint8_t  ampdu_tx_tids;
    uint8_t  last_rx_rate;
    uint8_t  last_rx_bw;
    uint8_t  last_rx_sgi;
    uint8_t  last_rx_ldpc;
    uint8_t  last_rx_stbc;
    uint32_t cur_rx_tp;
    uint32_t cur_tx_tp;
    uint32_t rx_xfers;
    uint32_t rx_xfer_max_len;
    uint32_t rx_xfer_max_pkts;
    uint32_t rx_xfer_pkts;
    uint64_t rx_xfer_bytes;
    uint16_t reg_trxdma_ctrl;
    uint16_t reserved;
    uint32_t reg_rxdma_agg_pg_th;
    uint32_t reg_rxdma_mode;
    uint32_t rx_skb_recycle;
    uint32_t rx_skb_allocs;
    uint32_t rx_cb_cnt;
    uint32_t rx_cb_ns_max;
    uint64_t rx_cb_ns;
    uint64_t rx_parse_ns;
    uint32_t rx_parse_ns_max;
    uint32_t rx_backlog_max;
    uint32_t ampdu_drop;
    uint32_t ampdu_forced_indicate;
    uint32_t ampdu_loss;
    uint32_t ampdu_window_shift;
    uint32_t rx_drop;
    uint32_t warm_start;
    uint32_t num_tx_ok_period;
    uint8_t  sta_ampdu_enable;
    uint8_t  tx_candidate_tids;
    uint8_t  drv_tx_bw_mode;
    uint8_t  drv_rx_ampdu_factor;
    uint8_t  fix_rx_ampdu_accept;
    uint8_t  reserved3[3];
    uint8_t  ap_vht_rx_map[2];
    uint8_t  ap_vht_tx_map[2];
    uint8_t  sta_vht_map[2];
    uint8_t  ra_rate_id;
    uint8_t  sta_mimo_type;
    uint8_t  ap_ht_mcs[4];
    uint8_t  reserved4[4];
    uint64_t ra_mask;
    uint32_t c2h_id_cnt[16];
    uint32_t c2h_id_other;
    uint32_t c2h_parse_fail;
    uint32_t ra_rpt_handled;
    uint8_t  ra_rpt_last[8];
    uint8_t  ra_rpt_last_len;
    uint8_t  reserved5[7];
};

struct rtl8814_link_status {
    uint32_t connected;
    uint32_t wpa2_state;
    int32_t  rssi;
    uint32_t channel;
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  rfe_type;
    uint8_t  rf_type;
    uint8_t  channel_plan;
    uint8_t  max_chan_nums;
    uint32_t rx_urb_submit_ok;
    uint32_t rx_urb_submit_err;
    uint32_t rx_urb_complete_ok;
    uint32_t rx_urb_complete_err;
    uint32_t rx_frames_decoded;
    uint32_t cmd_issued_cnt;
    uint32_t scan_state;
    uint32_t scanned_bss_cnt;
    struct rtl8814_trx_diag trx;
    uint32_t reg_rcr;
    uint16_t reg_rxfltmap0;
    uint16_t reg_rxfltmap2;
    uint32_t usb_speed;
    uint32_t bulk_in_mps;
    uint32_t rx_reassemble;
    uint32_t usb_switch;
    struct rtl8814_rate_diag rate;
};

struct rtl_uc_join_request {
    char     ssid[33];
    char     psk[64];
    uint32_t auth_mode;
};

/* vhtpriv.vht_highest_rate is an MGN_RATE (MGN_VHT1SS_MCS0 = 0xA0), not a DESC_RATE */
static const char *mgn_vht_str(uint8_t v, char *buf, size_t len) {
    if (v >= 0xA0 && v < 0xA0 + 40)
        snprintf(buf, len, "VHT %uSS MCS%u", (v - 0xA0) / 10 + 1, (v - 0xA0) % 10);
    else
        snprintf(buf, len, "MGN 0x%02X", v);
    return buf;
}

static unsigned vht_map_nss(const uint8_t *m) {
    unsigned n = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j += 2)
            if (((m[i] >> j) & 3) != 3)
                n++;
    return n;
}

static const char *sec_name(uint8_t sec) {
    switch (sec) {
    case 0: return "OPEN";
    case 1: return "WEP";
    case 2: return "WPA-PSK";
    case 3: return "WPA2-PSK";
    default: return "UNKNOWN";
    }
}

static const char *wpa2_state_name(uint32_t st) {
    switch (st) {
    case 0: return "IDLE";
    case 1: return "WAIT_EAPOL_MSG1";
    case 2: return "WAIT_EAPOL_MSG3";
    case 3: return "COMPLETED (KEYS INSTALLED)";
    case 4: return "FAILED (MIC ERROR)";
    default: return "UNKNOWN";
    }
}

/* DESC_RATE* (hal_com.h): 0-3 CCK, 4-11 OFDM, 12-43 HT MCS0-31, 44-83 VHT 1-4SS MCS0-9 */
static const char *desc_rate_str(uint8_t r, char *buf, size_t n) {
    static const char *legacy[] = { "1M", "2M", "5.5M", "11M", "6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M" };
    r &= 0x7F;
    if (r < 12)
        snprintf(buf, n, "%s", legacy[r]);
    else if (r < 44)
        snprintf(buf, n, "HT MCS%u", r - 12);
    else if (r < 84)
        snprintf(buf, n, "VHT %uSS MCS%u", (r - 44) / 10 + 1, (r - 44) % 10);
    else
        snprintf(buf, n, "0x%02X", r);
    return buf;
}

static const char *bw_str(uint8_t bw) {
    static const char *names[] = { "20", "40", "80", "160", "80+80", "5", "10" };
    if (bw == 0xFF) return "n/a";  /* 8814A FW RA report / RX desc carry no BW */
    return bw < 7 ? names[bw] : "?";
}

static void show_status(io_connect_t connect) {
    uint64_t vOut[2] = {0, 0};
    uint32_t vCount = 2;
    kern_return_t kr = IOConnectCallScalarMethod(connect, kRTLUC_GetVersion, NULL, 0, vOut, &vCount);
    if (kr == KERN_SUCCESS) {
        printf("[+] Selector 0 (GetVersion): version=0x%llx, phase=%lld\n", vOut[0], vOut[1]);
    }

    uint64_t rOut[6] = {0};
    uint32_t rCount = 6;
    kr = IOConnectCallScalarMethod(connect, kRTLUC_GetRadioInfo, NULL, 0, rOut, &rCount);
    if (kr == KERN_SUCCESS) {
        int probe_rc = (int)(int64_t)rOut[0];
        uint32_t sys_cfg = (uint32_t)rOut[1];
        uint8_t chip_type = (uint8_t)(rOut[2] & 0xFF);
        uint8_t cut_ver = (uint8_t)((rOut[2] >> 8) & 0xFF);
        int hal_init_status = (int)(int16_t)((rOut[2] >> 16) & 0xFFFF);

        uint32_t mac_hi = (uint32_t)rOut[3];
        uint16_t mac_lo = (uint16_t)(rOut[4] & 0xFFFF);
        uint8_t fw_ready = (uint8_t)((rOut[4] >> 32) & 0xFF);
        uint32_t phase = (uint32_t)rOut[5];

        uint8_t mac[6] = {
            (uint8_t)((mac_hi >> 24) & 0xFF),
            (uint8_t)((mac_hi >> 16) & 0xFF),
            (uint8_t)((mac_hi >> 8)  & 0xFF),
            (uint8_t)((mac_hi)       & 0xFF),
            (uint8_t)((mac_lo >> 8)  & 0xFF),
            (uint8_t)((mac_lo)       & 0xFF)
        };

        printf("\n================ RADIO TELEMETRY (Phase %u) ================\n", phase);
        printf("[+] Vendor Probe Return Code : %d (%s)\n", probe_rc, probe_rc == 0 ? "SUCCESS" : "FAILED");
        printf("[+] Register REG_SYS_CFG     : 0x%08X\n", sys_cfg);
        printf("[+] Chip Type                : %u (CHIP_8814A)\n", chip_type);
        printf("[+] Cut Version              : %u\n", cut_ver);
        printf("[+] Permanent Hardware MAC   : %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("[+] HAL Init Status          : %d (_SUCCESS = 1)\n", hal_init_status);
        printf("[+] Firmware Downloaded & OK : %s (flag=%u)\n", fw_ready ? "YES!" : "NO", fw_ready);
        printf("============================================================\n");
    }

    struct rtl8814_link_status st = {0};
    size_t stSize = sizeof(st);
    uint64_t lOut[4] = {0};
    uint32_t lCount = 4;
    kr = IOConnectCallMethod(connect, kRTLUC_GetLinkStatus, NULL, 0, NULL, 0, lOut, &lCount, &st, &stSize);
    if (kr == KERN_SUCCESS) {
        printf("\n==================== LINK & WPA2 STATUS ====================\n");
        printf("[+] 802.11 Link State        : %s\n", st.connected ? "ASSOCIATED (_FW_LINKED)" : "DISCONNECTED");
        printf("[+] WPA2 Handshake State     : %s (%u)\n", wpa2_state_name(st.wpa2_state), st.wpa2_state);
        printf("[+] Current Channel          : %u (Scan State=%u, BSS Count=%u)\n",
               st.channel, st.scan_state, st.scanned_bss_cnt);
        printf("[+] RF Config / RFE Type     : rf_type=%u, rfe_type=%u\n", st.rf_type, st.rfe_type);
        printf("[+] Channel Plan             : 0x%02X (%u channels active)\n", st.channel_plan, st.max_chan_nums);
        printf("[+] RX Bulk IN URBs          : submit_ok=%u, submit_err=%u, comp_ok=%u, comp_err=%u\n",
               st.rx_urb_submit_ok, st.rx_urb_submit_err, st.rx_urb_complete_ok, st.rx_urb_complete_err);
        printf("[+] RX Frames / H2C Cmds     : rx_frames=%u, cmd_issued=%u\n",
               st.rx_frames_decoded, st.cmd_issued_cnt);
        printf("[+] RX Drops                 : bad_urb_len=%u (last=%u), no_recvframe=%u, crc=%u, icv=%u,\n"
               "                               bad_pktlen=%u, runt=%u, alloc=%u\n",
               st.trx.urb_bad_len, st.trx.last_bad_urb_len, st.trx.drop_no_recvframe, st.trx.drop_crc,
               st.trx.drop_icv, st.trx.drop_bad_pktlen, st.trx.drop_runt, st.trx.drop_alloc);
        printf("[+] RX Report Types          : normal=%u, c2h=%u, other=%u\n",
               st.trx.rx_normal, st.trx.rx_c2h, st.trx.rx_other_rpt);
        printf("[+] RX 802.11 Frame Types    : mgmt=%u (beacon=%u, probe_rsp=%u), ctrl=%u, data=%u\n",
               st.trx.rx_mgmt, st.trx.rx_beacon, st.trx.rx_probe_rsp, st.trx.rx_ctrl, st.trx.rx_data);
        printf("[+] TX Bulk OUT Writes       : ok=%u, err=%u (last_err=%d, last_ep_addr=%u)\n",
               st.trx.tx_write_ok, st.trx.tx_write_err, st.trx.tx_last_err, st.trx.tx_last_ep);
        printf("[+] USB Link (HAL view)      : usb_speed=%u (%s), bulk_in_mps=%u\n", st.usb_speed,
               st.usb_speed == 3 ? "SuperSpeed" : st.usb_speed == 2 ? "High-Speed" : st.usb_speed == 1 ? "Full-Speed" : "unknown",
               st.bulk_in_mps);
        printf("[+] USB3 Mode Switch         : efuse_allows=%s, switch_issued=%s, ss_finalized=%s\n",
               (st.usb_switch & 1) ? "yes" : "no", (st.usb_switch & 2) ? "yes" : "no",
               (st.usb_switch & 4) ? "yes" : "no");
        printf("[+] RX Reassembly            : %s (carry=%u, reassembled=%u, stage_dropped=%u)\n",
               st.rx_reassemble ? "ON" : "off", st.trx.rx_carry, st.trx.rx_reassembled, st.trx.rx_stage_dropped);
        printf("[+] RX Filter Registers      : RCR=0x%08X, RXFLTMAP0(mgmt)=0x%04X, RXFLTMAP2(data)=0x%04X\n",
               st.reg_rcr, st.reg_rxfltmap0, st.reg_rxfltmap2);
        printf("[+] Last RX Desc (len=%-5u) :", st.trx.last_urb_len);
        for (int i = 0; i < 24; i++)
            printf("%s%02X", (i % 4) == 0 ? "  " : " ", st.trx.last_desc[i]);
        printf("\n");
        {
            struct rtl8814_rate_diag *r = &st.rate;
            char a[24], b[24], c[24];
            printf("[+] PHY Mode                 : wireless_mode=0x%02X, HT=%u, VHT=%u, chan_bw=%s MHz (offset=%u), sta_bw=%s, ra_bw=%s\n",
                   r->wireless_mode, r->ht_option, r->vht_option, bw_str(r->cur_bw), r->cur_ch_offset,
                   bw_str(r->sta_bw), bw_str(r->ra_bw));
            printf("[+] NSS (HAL)                : tx_nss=%u, rx_nss=%u, vht_highest=%s, vht_sgi80=%u, vht_ampdu_len=%u\n",
                   r->hal_tx_nss, r->hal_rx_nss, mgn_vht_str(r->vht_highest_rate, a, sizeof(a)),
                   r->vht_sgi_80m, r->vht_ampdu_len);
            printf("[+] TX Rate (FW RA)          : %s %s, tx_bw=%s MHz\n",
                   desc_rate_str(r->tx_rate, a, sizeof(a)), r->tx_sgi ? "SGI" : "LGI", bw_str(r->tx_bw));
            printf("[+] RX Rate                  : sta=%s, last_desc=%s bw=%s sgi=%u ldpc=%u stbc=%u\n",
                   desc_rate_str(r->rx_rate, b, sizeof(b)), desc_rate_str(r->last_rx_rate, c, sizeof(c)),
                   bw_str(r->last_rx_bw), r->last_rx_sgi, r->last_rx_ldpc, r->last_rx_stbc);
            printf("[+] VHT MCS Maps             : ap_rx=%02X %02X (%uSS), ap_tx=%02X %02X, negotiated=%02X %02X (%uSS)  [2 bits/SS: 0=MCS0-7 1=0-8 2=0-9 3=none]\n",
                   r->ap_vht_rx_map[0], r->ap_vht_rx_map[1], vht_map_nss(r->ap_vht_rx_map),
                   r->ap_vht_tx_map[0], r->ap_vht_tx_map[1],
                   r->sta_vht_map[0], r->sta_vht_map[1], vht_map_nss(r->sta_vht_map));
            printf("[+] RA Mask (to FW)          : 0x%016llX rate_id=%u mimo_type=%u, AP HT MCS=%02X %02X %02X %02X\n",
                   (unsigned long long)r->ra_mask, r->ra_rate_id, r->sta_mimo_type,
                   r->ap_ht_mcs[0], r->ap_ht_mcs[1], r->ap_ht_mcs[2], r->ap_ht_mcs[3]);
            printf("[+] C2H by id                :");
            for (int i = 0; i < 16; i++)
                if (r->c2h_id_cnt[i])
                    printf(" 0x%02X=%u", i, r->c2h_id_cnt[i]);
            printf(" other=%u parse_fail=%u\n", r->c2h_id_other, r->c2h_parse_fail);
            printf("[+] FW RA Reports            : handled=%u, last(len=%u):", r->ra_rpt_handled, r->ra_rpt_last_len);
            for (int i = 0; i < 8; i++)
                printf(" %02X", r->ra_rpt_last[i]);
            printf("\n");
            printf("[+] A-MPDU / BA              : rx_ba_tids=0x%02X (max wsize=%u), tx_ampdu_tids=0x%02X, tx_candidates=0x%02X, sta_ampdu_enable=%u, tx_ok_period=%u\n",
                   r->ba_rx_tids, r->ba_rx_wsize, r->ampdu_tx_tids, r->tx_candidate_tids, r->sta_ampdu_enable,
                   r->num_tx_ok_period);
            printf("[+] Driver Defaults          : tx_bw_mode=0x%02X, rx_ampdu_factor=0x%02X, rx_ampdu_accept=0x%02X (0xFF = auto)\n",
                   r->drv_tx_bw_mode, r->drv_rx_ampdu_factor, r->fix_rx_ampdu_accept);
            printf("[+] Driver Throughput        : rx=%u Mbps, tx=%u Mbps (watchdog)\n", r->cur_rx_tp, r->cur_tx_tp);
            printf("[+] RX USB Aggregation       : xfers=%u, avg %.1f frames / %.0f B per xfer, max %u frames / %u B\n",
                   r->rx_xfers, r->rx_xfers ? (double)r->rx_xfer_pkts / r->rx_xfers : 0.0,
                   r->rx_xfers ? (double)r->rx_xfer_bytes / r->rx_xfers : 0.0,
                   r->rx_xfer_max_pkts, r->rx_xfer_max_len);
            printf("[+] RX Agg Registers         : TRXDMA_CTRL(0x10C)=0x%04X (RXDMA_AGG_EN=%u), AGG_PG_TH(0x280)=0x%08X (USB_AGG_EN=%u), RXDMA_MODE(0x290)=0x%08X\n",
                   r->reg_trxdma_ctrl, (r->reg_trxdma_ctrl >> 2) & 1, r->reg_rxdma_agg_pg_th,
                   (r->reg_rxdma_agg_pg_th >> 31) & 1, r->reg_rxdma_mode);
            printf("[+] RX Path Cost             : recycle=%s, 32KB_allocs=%u, completion avg %.1f us (max %.1f), parse avg %.1f us/xfer (max %.1f), backlog_max=%u\n",
                   r->rx_skb_recycle ? "ON" : "off", r->rx_skb_allocs,
                   r->rx_cb_cnt ? r->rx_cb_ns / 1000.0 / r->rx_cb_cnt : 0.0, r->rx_cb_ns_max / 1000.0,
                   r->rx_xfers ? r->rx_parse_ns / 1000.0 / r->rx_xfers : 0.0, r->rx_parse_ns_max / 1000.0,
                   r->rx_backlog_max);
            printf("[+] RX Reorder (A-MPDU)      : drop=%u, forced_indicate=%u, loss=%u, window_shift=%u, rx_drop=%u\n",
                   r->ampdu_drop, r->ampdu_forced_indicate, r->ampdu_loss, r->ampdu_window_shift, r->rx_drop);
            printf("[+] Probe Power State         : SYS_CLKR+1=0x%02X, CR=0x%02X -> %s\n",
                   r->warm_start & 0xFF, (r->warm_start >> 8) & 0xFF,
                   (r->warm_start >> 16) & 1 ? "chip was already on, powered off before HAL init (warm start)" : "cold (chip was off)");
        }
        if (st.connected) {
            printf("[+] Connected SSID           : %s\n", st.ssid);
            printf("[+] Connected BSSID          : %02X:%02X:%02X:%02X:%02X:%02X\n",
                   st.bssid[0], st.bssid[1], st.bssid[2], st.bssid[3], st.bssid[4], st.bssid[5]);
            printf("[+] Signal RSSI              : %d dBm\n", st.rssi);
        }
        printf("============================================================\n");
    }
}

static void run_scan(io_connect_t connect) {
    printf("[*] Triggering 802.11 site survey across 2.4 GHz & 5 GHz channels...\n");
    kern_return_t kr = IOConnectCallScalarMethod(connect, kRTLUC_TriggerScan, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("[-] TriggerScan returned 0x%08x (scan may already be in progress)\n", kr);
    }

    for (int i = 5; i > 0; i--) {
        printf("    Scanning... (%d s remaining)\r", i);
        fflush(stdout);
        sleep(1);
    }
    printf("\n");

    struct rtl_scan_entry entries[RTL_MAX_SCAN_ENTRIES];
    memset(entries, 0, sizeof(entries));
    size_t outSize = sizeof(entries);
    uint64_t countOut[1] = {0};
    uint32_t countCnt = 1;

    kr = IOConnectCallMethod(connect, kRTLUC_GetScanResult,
                             NULL, 0, NULL, 0,
                             countOut, &countCnt,
                             entries, &outSize);
    if (kr != KERN_SUCCESS) {
        printf("[-] GetScanResult failed: 0x%08x\n", kr);
        return;
    }

    uint32_t count = (uint32_t)countOut[0];
    printf("\n================ SCAN RESULTS (%u networks) ================\n", count);
    printf("%-32s  %-17s  %-4s  %-8s  %-10s\n", "SSID", "BSSID", "CH", "RSSI", "SECURITY");
    printf("--------------------------------------------------------------------------------\n");
    for (uint32_t i = 0; i < count && i < RTL_MAX_SCAN_ENTRIES; i++) {
        printf("%-32s  %02X:%02X:%02X:%02X:%02X:%02X  %-4u  %-4d dBm  %-10s\n",
               entries[i].ssid[0] ? entries[i].ssid : "<Hidden SSID>",
               entries[i].bssid[0], entries[i].bssid[1], entries[i].bssid[2],
               entries[i].bssid[3], entries[i].bssid[4], entries[i].bssid[5],
               entries[i].channel, entries[i].rssi, sec_name(entries[i].security));
    }
    printf("================================================================================\n");
}

static void run_join(io_connect_t connect, const char *ssid, const char *psk) {
    struct rtl_uc_join_request req;
    memset(&req, 0, sizeof(req));
    strncpy(req.ssid, ssid, sizeof(req.ssid) - 1);
    if (psk && psk[0]) {
        strncpy(req.psk, psk, sizeof(req.psk) - 1);
        req.auth_mode = 1; /* WPA2-PSK */
    } else {
        req.auth_mode = 0; /* Open */
    }

    printf("[*] Joining SSID '%s' (%s)...\n", req.ssid, req.auth_mode ? "WPA2-PSK" : "Open");
    kern_return_t kr = IOConnectCallStructMethod(connect, kRTLUC_JoinNetwork,
                                                 &req, sizeof(req), NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("[-] JoinNetwork failed: 0x%08x\n", kr);
        return;
    }

    for (int i = 0; i < 15; i++) {
        usleep(500000);
        struct rtl8814_link_status st = {0};
        size_t stSize = sizeof(st);
        uint64_t lOut[4] = {0};
        uint32_t lCount = 4;
        if (IOConnectCallMethod(connect, kRTLUC_GetLinkStatus, NULL, 0, NULL, 0,
                                lOut, &lCount, &st, &stSize) == KERN_SUCCESS) {
            printf("    [%2d/15] Link=%u, Channel=%u, WPA2=%s\n",
                   i + 1, st.connected, st.channel, wpa2_state_name(st.wpa2_state));
            if (st.connected && (req.auth_mode == 0 || st.wpa2_state == 3)) {
                printf("\n[+] CONNECTED to '%s' (%02X:%02X:%02X:%02X:%02X:%02X) on Channel %u!\n",
                       st.ssid,
                       st.bssid[0], st.bssid[1], st.bssid[2],
                       st.bssid[3], st.bssid[4], st.bssid[5],
                       st.channel);
                printf("[+] Run: sudo ipconfig set en2 DHCP\n");
                return;
            }
            if (st.wpa2_state == 4) {
                printf("\n[-] WPA2 4-Way Handshake FAILED (check password).\n");
                return;
            }
        }
    }
    printf("[!] Join still in progress or timed out; check './Tools/test_uc status'.\n");
}

static int reg_access(io_connect_t connect, uint32_t op, uint32_t width, uint32_t addr, uint32_t value, uint32_t *out) {
    uint64_t in[4] = { op, width, addr, value };
    uint64_t o[1] = {0};
    uint32_t oc = 1;
    kern_return_t kr = IOConnectCallScalarMethod(connect, kRTLUC_RegAccess, in, 4, o, &oc);
    if (kr != KERN_SUCCESS) {
        printf("[-] RegAccess failed: 0x%08x (kext too old or bad address)\n", kr);
        return -1;
    }
    *out = (uint32_t)o[0];
    return 0;
}

static void show_rxlog(io_connect_t connect) {
    struct rtl8814_rx_log log;
    size_t sz = sizeof(log);
    memset(&log, 0, sizeof(log));
    kern_return_t kr = IOConnectCallStructMethod(connect, kRTLUC_GetRxLog, NULL, 0, &log, &sz);
    if (kr != KERN_SUCCESS) {
        printf("[-] GetRxLog failed: 0x%08x (kext too old?)\n", kr);
        return;
    }
    uint32_t n = log.next_seq < RX_XFER_LOG_N ? log.next_seq : RX_XFER_LOG_N;
    printf("Last %u bulk IN transfers (oldest first):\n", n);
    for (uint32_t k = log.next_seq - n; k < log.next_seq; k++) {
        struct rtl8814_rx_xfer *x = &log.xfer[k % RX_XFER_LOG_N];
        uint32_t m = x->len < 32 ? x->len : 32;
        printf("#%-6u len=%-5u head:", x->seq, x->len);
        for (uint32_t i = 0; i < m; i++) printf(" %02X", x->head[i]);
        printf("\n%*s tail:", 19, "");
        for (uint32_t i = 0; i < m; i++) printf(" %02X", x->tail[i]);
        printf("\n");
    }
}

static void show_joinlog(io_connect_t connect) {
    static const char *names[] = {
        "?", "radio join", "radio leave", "do_join", "select", "survey for join", "clnt join",
        "auth rx", "assoc rx", "deauth rx", "disassoc rx", "link timer", "join res",
        "joinbss evt", "join timeout", "disconnect hdl", "indicate disc", "wpa2", "survey done",
	        "auth tx", "mgmt rx", "regs", "flush scanq", "SAE auth rx", "SAE auth tx", "SAE step", "PMF", "CSA",
	    };
    static struct rtl_jlog log;
    size_t sz = sizeof(log);
    memset(&log, 0, sizeof(log));
    kern_return_t kr = IOConnectCallStructMethod(connect, kRTLUC_GetJoinLog, NULL, 0, &log, &sz);
    if (kr != KERN_SUCCESS) {
        printf("[-] GetJoinLog failed: 0x%08x (kext too old?)\n", kr);
        return;
    }
    uint32_t n = log.next < RTL_JLOG_N ? log.next : RTL_JLOG_N;
    uint32_t t0 = n ? log.e[(log.next - n) % RTL_JLOG_N].t_ms : 0;
    printf("Last %u join events of %u (oldest first, ms since the first shown):\n", n, log.next);
    for (uint32_t k = log.next - n; k < log.next; k++) {
        struct rtl_jlog_ev *e = &log.e[k % RTL_JLOG_N];
        const char *nm = e->ev < sizeof(names) / sizeof(names[0]) ? names[e->ev] : "?";
        if (e->ev == JL_SAE_STEP) {
            static const char *steps[] = { "?", "prepare", "start", "commit tx", "token",
                "peer commit", "confirm tx", "peer confirm", "keys", "assoc", "retry",
                "FAIL", "stale", "compute ms" };
            printf("%8u  %-16s %s result=%d\n", e->t_ms - t0, nm,
                   e->a < sizeof(steps) / sizeof(steps[0]) ? steps[e->a] : "?", (int)e->b);
            continue;
        }
        if (e->ev == JL_PMF) {
            static const char *kinds[] = { "?", "IGTK installed", "unicast verified",
                "unicast CCMP FAIL", "replay drop", "unprotected deauth dropped",
                "group BIP ok", "group BIP FAIL", "sent protected",
                "SA Query answered" };
            unsigned k = (unsigned)e->a;
            if (k == RTL_PMF_DEC_OK || k == RTL_PMF_UNPROT_DROP || k == RTL_PMF_TX_PROTECTED)
                printf("%8u  %-16s %s subtype=%d reason=%d\n", e->t_ms - t0, nm, kinds[k],
                       e->b & 0xff, (int)((unsigned)e->b >> 8));
            else
                printf("%8u  %-16s %s %d\n", e->t_ms - t0, nm,
                       k < sizeof(kinds) / sizeof(kinds[0]) ? kinds[k] : "?", e->b);
            continue;
        }
        if (e->ev == JL_CSA) {
            static const char *kinds[] = { "?", "channel switch announced", "followed channel switch",
                "channel switch ignored" };
            printf("%8u  %-16s %s ch=%d bw=%d count=%d\n", e->t_ms - t0, nm,
                   e->a < 4 ? kinds[e->a] : "?", e->b & 0xff, (e->b >> 8) & 0xff, (e->b >> 16) & 0xff);
            continue;
        }
        if (e->ev == JL_SAE_AUTH_RX || e->ev == JL_SAE_AUTH_TX) {
            const char *note = e->b == 126 ? " (H2E)" : e->b == 127 ? " (SAE-PK)" : "";
            const char *seq = e->a == 1 ? "commit" : e->a == 2 ? "confirm" : "unknown";
            printf("%8u  %-16s seq=%d (%s) status=%d%s\n", e->t_ms - t0,
                   nm, e->a, seq, e->b, note);
        } else {
            printf("%8u  %-16s a=%-8d (0x%x) b=%d\n", e->t_ms - t0, nm, e->a, (unsigned)e->a, e->b);
        }
    }
}

/* ---- radio ABI (kRTLUC_RadioCall -> callPlatformFunction), what the IO80211 layer will use ---- */
enum { R_ABI, R_SCAN_START, R_SCAN_COPY, R_CHANNELS, R_JOIN, R_STATUS, R_LEAVE, R_MAC };

static kern_return_t radio_call(io_connect_t c, uint64_t op, const void *in, size_t inSz,
                                void *out, size_t *outSz, uint64_t *count) {
    uint64_t o[1] = {0};
    uint32_t oc = 1;
    kern_return_t kr = IOConnectCallMethod(c, kRTLUC_RadioCall, &op, 1, in, inSz, o, &oc, out, outSz);
    if (count)
        *count = o[0];
    return kr;
}

static const char *radio_state_str(uint32_t s) {
    static const char *n[] = { "idle", "scanning", "joining", "linked" };
    return s < 4 ? n[s] : "?";
}

static int radio_cmd(io_connect_t c, int argc, char **argv) {
    const char *sub = argc >= 3 ? argv[2] : "status";
    kern_return_t kr;
    char a[32], b[32];

    if (strcmp(sub, "abi") == 0) {
        uint64_t abi = 0;
        kr = radio_call(c, R_ABI, NULL, 0, NULL, NULL, &abi);
        printf("[%c] radio ABI: %llu (tool built for %d), kr=0x%x\n", kr ? '-' : '+',
               (unsigned long long)abi, RTL_RADIO_ABI, kr);
        return kr != KERN_SUCCESS;
    }
    if (strcmp(sub, "status") == 0) {
        struct rtl_radio_status st;
        size_t sz = sizeof(st);
        memset(&st, 0, sizeof(st));
        kr = radio_call(c, R_STATUS, NULL, 0, &st, &sz, NULL);
        if (kr) { printf("[-] status kr=0x%x\n", kr); return 1; }
        printf("[+] state=%s port_valid=%u rsn=%u hook=%u scanning=%u\n", radio_state_str(st.state),
               st.port_valid, st.rsn, st.hook_attached, st.scanning);
        if (st.state == RTL_RADIO_S_LINKED)
            printf("[+] '%.*s' %02x:%02x:%02x:%02x:%02x:%02x ch %u (%s, %s MHz, center %u) rssi %d dBm nss %u\n"
                   "[+] tx %s %s, rx %s %s bw %s\n",
                   st.ssid_len, st.ssid, st.bssid[0], st.bssid[1], st.bssid[2], st.bssid[3], st.bssid[4],
                   st.bssid[5], st.channel, st.band ? "5G" : "2G", bw_str(st.bss_bw), st.center,
                   st.rssi_dbm, st.nss, desc_rate_str(st.tx_rate, a, sizeof(a)), st.tx_sgi ? "SGI" : "LGI",
                   desc_rate_str(st.rx_rate, b, sizeof(b)), st.rx_sgi ? "SGI" : "LGI", bw_str(st.rx_bw));
        return 0;
    }
    if (strcmp(sub, "mac") == 0) {
        uint8_t m[6] = {0};
        size_t sz = 6;
        kr = radio_call(c, R_MAC, NULL, 0, m, &sz, NULL);
        printf("[%c] mac %02x:%02x:%02x:%02x:%02x:%02x kr=0x%x\n", kr ? '-' : '+',
               m[0], m[1], m[2], m[3], m[4], m[5], kr);
        return kr != KERN_SUCCESS;
    }
    if (strcmp(sub, "channels") == 0) {
        struct rtl_radio_chan ch[64];
        size_t sz = sizeof(ch);
        uint64_t n = 0;
        kr = radio_call(c, R_CHANNELS, NULL, 0, ch, &sz, &n);
        if (kr) { printf("[-] channels kr=0x%x\n", kr); return 1; }
        printf("[+] %llu channels:", (unsigned long long)n);
        for (uint64_t i = 0; i < n; i++)
            printf(" %u%s", ch[i].channel, ch[i].passive ? "p" : "");
        printf("\n");
        return 0;
    }
    if (strcmp(sub, "scan") == 0) {
        static struct rtl_radio_bss bss[64];
        size_t sz = sizeof(bss);
        uint64_t n = 0;
        kr = radio_call(c, R_SCAN_START, NULL, 0, NULL, NULL, NULL);
        printf("[%c] scan start kr=0x%x\n", kr ? '!' : '+', kr);
        sleep(4);
        kr = radio_call(c, R_SCAN_COPY, NULL, 0, bss, &sz, &n);
        if (kr) { printf("[-] scan copy kr=0x%x\n", kr); return 1; }
        printf("[+] %llu BSS (%zu bytes)\n", (unsigned long long)n, sz);
        for (uint64_t i = 0; i < n; i++)
            printf("    %02x:%02x:%02x:%02x:%02x:%02x ch %3u %4d dBm sec %u cap 0x%04x int %u ies %4u '%.*s'\n",
                   bss[i].bssid[0], bss[i].bssid[1], bss[i].bssid[2], bss[i].bssid[3], bss[i].bssid[4],
                   bss[i].bssid[5], bss[i].channel, bss[i].rssi_dbm, bss[i].security, bss[i].capinfo,
                   bss[i].intval, bss[i].ies_len, bss[i].ssid_len, bss[i].ssid);
        return 0;
    }
    if (strcmp(sub, "leave") == 0) {
        kr = radio_call(c, R_LEAVE, NULL, 0, NULL, NULL, NULL);
        printf("[%c] leave kr=0x%x\n", kr ? '-' : '+', kr);
        return kr != KERN_SUCCESS;
    }
    if (strcmp(sub, "join") == 0 && argc >= 4) {
        /* WPA2 test path: derive the PMK here; the passphrase never reaches the kext. */
        struct rtl_radio_join j;
        memset(&j, 0, sizeof(j));
        size_t sl = strlen(argv[3]);
        if (sl == 0 || sl > 32) { printf("[-] bad SSID\n"); return 1; }
        j.ssid_len = (uint8_t)sl;
        memcpy(j.ssid, argv[3], sl);
        if (argc >= 5) {
            if (CCKeyDerivationPBKDF(kCCPBKDF2, argv[4], strlen(argv[4]), j.ssid, sl,
                                     kCCPRFHmacAlgSHA1, 4096, j.pmk, 32) != 0) {
                printf("[-] PBKDF2 failed\n");
                return 1;
            }
            j.has_pmk = 1;
        }
        if (argc >= 6 && sscanf(argv[5], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &j.bssid[0], &j.bssid[1],
                                &j.bssid[2], &j.bssid[3], &j.bssid[4], &j.bssid[5]) != 6) {
            printf("[-] bad BSSID\n");
            return 1;
        }
        kr = radio_call(c, R_JOIN, &j, sizeof(j), NULL, NULL, NULL);
        memset(&j, 0, sizeof(j));
        printf("[%c] join queued kr=0x%x; waiting for the link...\n", kr ? '-' : '+', kr);
        for (int i = 0; kr == KERN_SUCCESS && i < 20; i++) {
            struct rtl_radio_status st;
            size_t sz = sizeof(st);
            sleep(1);
            if (radio_call(c, R_STATUS, NULL, 0, &st, &sz, NULL) == KERN_SUCCESS &&
                st.state == RTL_RADIO_S_LINKED && st.port_valid) {
                printf("[+] linked, port valid (ch %u, rssi %d dBm)\n", st.channel, st.rssi_dbm);
                return 0;
            }
        }
        return 1;
    }
    printf("radio subcommands: abi | status | mac | channels | scan | leave | join <SSID> [PSK] [BSSID]\n");
    return 1;
}

int main(int argc, char **argv) {
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RTL8814Native"));
    if (!service) {
        printf("[-] RTL8814Native service not found in IORegistry\n");
        return 1;
    }

    io_connect_t connect;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &connect);
    if (kr != KERN_SUCCESS) {
        printf("[-] IOServiceOpen failed: 0x%08x\n", kr);
        IOObjectRelease(service);
        return 1;
    }

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        show_status(connect);
    } else if (strcmp(argv[1], "scan") == 0) {
        run_scan(connect);
    } else if (strcmp(argv[1], "join") == 0 && argc >= 3) {
        run_join(connect, argv[2], (argc >= 4) ? argv[3] : "");
    } else if (strcmp(argv[1], "peek") == 0 && argc >= 3) {
        uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t w = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 4;
        uint32_t v;
        if (reg_access(connect, 0, w, addr, 0, &v) == 0)
            printf("[0x%04X] = 0x%0*X\n", addr, (int)(w * 2), v);
    } else if (strcmp(argv[1], "poke") == 0 && argc >= 4) {
        uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t val = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t w = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : 4;
        uint32_t v;
        if (reg_access(connect, 1, w, addr, val, &v) == 0 && reg_access(connect, 0, w, addr, 0, &v) == 0)
            printf("[0x%04X] <- 0x%X, readback 0x%0*X\n", addr, val, (int)(w * 2), v);
    } else if (strcmp(argv[1], "rf") == 0 && argc >= 4) {
        uint32_t path = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t reg = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t v;
        if (reg_access(connect, 2, 4, (path << 8) | (reg & 0xFF), 0, &v) == 0)
            printf("RF[%c][0x%02X] = 0x%05X\n", 'A' + (int)path, reg, v);
    } else if (strcmp(argv[1], "joinlog") == 0) {
        show_joinlog(connect);
    } else if (strcmp(argv[1], "rxlog") == 0) {
        show_rxlog(connect);
    } else if (strcmp(argv[1], "recycle") == 0 && argc >= 3) {
        uint32_t prev;
        if (reg_access(connect, 3, 4, 1, (uint32_t)strtoul(argv[2], NULL, 0), &prev) == 0)
            printf("[+] RX skb recycle: %s -> %s\n", prev ? "ON" : "off", strtoul(argv[2], NULL, 0) ? "ON" : "off");
    } else if (strcmp(argv[1], "perfreset") == 0) {
        uint32_t prev;
        if (reg_access(connect, 3, 4, 2, 0, &prev) == 0)
            printf("[+] RX perf counters reset\n");
    } else if (strcmp(argv[1], "reassemble") == 0 && argc >= 3) {
        uint32_t prev;
        if (reg_access(connect, 3, 4, 0 /* RTL_OPT_RX_REASSEMBLE */, (uint32_t)strtoul(argv[2], NULL, 0), &prev) == 0)
            printf("[+] RX reassembly: %u -> %s\n", prev, strtoul(argv[2], NULL, 0) ? "ON" : "off");
    } else if (strcmp(argv[1], "radio") == 0) {
        int rc = radio_cmd(connect, argc, argv);
        IOServiceClose(connect);
        IOObjectRelease(service);
        return rc;
    } else if (strcmp(argv[1], "disconnect") == 0) {
        IOConnectCallScalarMethod(connect, kRTLUC_Disconnect, NULL, 0, NULL, NULL);
        printf("[+] Disconnected.\n");
    } else {
        printf("Usage:\n");
        printf("  %s [status]\n", argv[0]);
        printf("  %s scan\n", argv[0]);
        printf("  %s join <SSID> [PSK]\n", argv[0]);
        printf("  %s disconnect\n", argv[0]);
        printf("  %s radio [abi|status|mac|channels|scan|leave|join <SSID> [PSK] [BSSID]]\n", argv[0]);
        printf("  %s peek <addr> [width]\n", argv[0]);
        printf("  %s poke <addr> <val> [width]\n", argv[0]);
        printf("  %s rf <path 0-3> <reg>\n", argv[0]);
        printf("  %s rxlog\n", argv[0]);
        printf("  %s reassemble <0|1>\n", argv[0]);
        printf("  %s recycle <0|1>\n", argv[0]);
        printf("  %s perfreset\n", argv[0]);
    }

    IOServiceClose(connect);
    IOObjectRelease(service);
    return 0;
}
