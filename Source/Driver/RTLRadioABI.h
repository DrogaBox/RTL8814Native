/*
 * RTL8814Native — radio ABI
 *
 * A small, stable surface for a separate kext (the planned native IO80211
 * layer, Phase 5) to drive the radio without linking against it. Calls go
 * through IOService::callPlatformFunction on the RTL8814Native instance:
 *
 *   drv->callPlatformFunction(RTL_RADIO_FN_STATUS, false, &st, NULL, NULL, NULL);
 *
 * The header is plain C with fixed-width fields so both kexts (and test_uc,
 * through kRTLUC_RadioCall) share it. Bump RTL_RADIO_ABI on any layout change.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _RTL8814_RADIO_ABI_H
#define _RTL8814_RADIO_ABI_H

#include <stdint.h>

#define RTL_RADIO_ABI             2
#define RTL_RADIO_SAE_PASSWORD_MAX 63

/* Function names (OSSymbol keys for callPlatformFunction); p1..p4 below */
#define RTL_RADIO_FN_ABI          "rtlRadioAbi"        /* p1: uint32_t *abi (out) */
#define RTL_RADIO_FN_SCAN_START   "rtlRadioScanStart"  /* p1: const uint32_t *channels or NULL (whole plan), p2: const uint32_t *count;
                                                        * channels off the plan are dropped, none left scans nothing;
                                                        * kIOReturnBusy if one is running. Older drivers ignore p1/p2.
                                                        * p3: const uint32_t *flags or NULL, RTL_RADIO_SCAN_*; older
                                                        * drivers ignore it and scan actively. */
#define RTL_RADIO_SCAN_PASSIVE    0x1                  /* no probe requests, no TX power load on the scan channels */
#define RTL_RADIO_FN_SCAN_COPY    "rtlRadioScanCopy"   /* p1: struct rtl_radio_bss[], p2: uint32_t *count (in: capacity, out: filled) */
#define RTL_RADIO_FN_CHANNELS     "rtlRadioChannels"   /* p1: struct rtl_radio_chan[], p2: uint32_t *count (in/out) */
#define RTL_RADIO_FN_JOIN         "rtlRadioJoin"       /* p1: struct rtl_radio_join * (SAE password consumed and wiped) */
#define RTL_RADIO_FN_STATUS       "rtlRadioStatus"     /* p1: struct rtl_radio_status * (out) */
#define RTL_RADIO_FN_LEAVE        "rtlRadioLeave"      /* no args */
#define RTL_RADIO_FN_MAC          "rtlRadioMac"        /* p1: uint8_t[6] (out) */
/* p1: const uint8_t[6]. Not while linked or joining (kIOReturnBusy); builds
 * before 4.15 answer kIOReturnUnsupported. Lasts until the kext unloads. */
#define RTL_RADIO_FN_SET_MAC      "rtlRadioSetMac"
/*
 * Data path. While a hook is attached every received data frame (Ethernet
 * framing) goes to the hook as an mbuf the hook then owns, and en2 reports
 * its link inactive. The hook runs on the RX workqueue thread: it must not
 * block or call back into the radio.
 */
#define RTL_RADIO_FN_DATA_HOOK    "rtlRadioDataHook"   /* p1: rtl_radio_rx_fn (NULL = detach), p2: void *ctx */
#define RTL_RADIO_FN_TX           "rtlRadioTx"         /* p1: mbuf_t, Ethernet frame (consumed in all cases) */

typedef void (*rtl_radio_rx_fn)(void *ctx, void *mbuf);

/*
 * AWDL. While attached, every received 802.11 frame whose A3 is the AWDL
 * BSSID 00:25:00:FF:94:73 is passed to the hook as raw bytes (header
 * included, FCS stripped) and dropped by the radio afterwards. The bytes are
 * only valid during the call: the hook copies what it keeps. Same lock rules
 * as the data hook; it runs on the RX workqueue thread, and with
 * rtlawdl_sim=1 also on a user client thread (RTLAwdlSim.h), never both at
 * once. Older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_AWDL_HOOK    "rtlRadioAwdlHook"   /* p1: rtl_radio_awdl_rx_fn (NULL = detach), p2: void *ctx */

typedef void (*rtl_radio_awdl_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len,
                                     int8_t rssi_dbm, uint8_t channel);

/*
 * One management frame, or one AWDL data frame (data or QoS data, To/From DS
 * 0, A3 = 00:25:00:FF:94:73), 802.11 header and body, no FCS, sent on the
 * current operating channel at the management rate (OFDM 6 Mb/s, 12 for data,
 * on the AWDL BSSID). The radio sets the sequence number; group-addressed
 * frames go without retries. Data frames get kIOReturnNoResources while all
 * but 16 of the radio's management buffers are in use. Builds before
 * AWDL data TX answer kIOReturnBadArgument for a data frame. The bytes are copied
 * before the call returns. kIOReturnNotReady while the radio is down,
 * kIOReturnBusy while TX waits for a channel, kIOReturnNoResources with no
 * free management buffer. Must not be called from inside a radio hook.
 */
#define RTL_RADIO_FN_TX_80211     "rtlRadioTx80211"    /* p1: const uint8_t *frame, p2: uint32_t *len */

/*
 * Hardware port 1 in ad-hoc mode on the AWDL BSSID with the given MAC, so
 * unicast frames to that address get a hardware ACK; data frames of other
 * BSSIDs are then accepted while it is on. NULL turns the port off. Writes
 * registers over USB: not from inside a radio hook.
 */
#define RTL_RADIO_FN_AWDL_PORT    "rtlRadioAwdlPort"   /* p1: const uint8_t mac[6] or NULL */
/*
 * Rate of unicast AWDL data frames, as a TX descriptor rate index: 0x04..0x0b
 * OFDM 6..54 Mb/s, 0x0c..0x13 HT MCS 0..7; 0 goes back to the fixed 12 Mb/s.
 * While one is set each such frame asks the chip for a CCX TX report and may
 * fall back to slower rates on its retries, no lower than OFDM 6 Mb/s; the
 * reports add up in rtl_radio_awdl_txstat. Lasts until changed or the kext
 * unloads. Older builds answer kIOReturnUnsupported to both.
 */
#define RTL_RADIO_FN_AWDL_RATE    "rtlRadioAwdlRate"   /* p1: const uint32_t *rate */
#define RTL_RADIO_FN_AWDL_TXSTAT  "rtlRadioAwdlTxStat" /* p1: struct rtl_radio_awdl_txstat * (out) */
struct rtl_radio_awdl_txstat {
	uint32_t clean;            /* acknowledged at the first try */
	uint32_t retried;          /* acknowledged after one retry */
	uint32_t retried2;         /* acknowledged after two or more */
	uint32_t lost;             /* retry limit or lifetime reached */
	uint32_t rate;             /* final rate of the last report (descriptor index) */
	uint32_t reports;          /* all of them, since the kext loaded */
};
/*
 * Delivery of the unicast AWDL action frames (the UMIs). Each one asks the
 * chip for a CCX TX report, tagged with a number the report carries back.
 * The results sit in a ring of the last RTL_RADIO_AFSTAT_RING, the newest at
 * (head - 1) % RTL_RADIO_AFSTAT_RING. A caller keeps the head it last read
 * and takes the entries after it; more than the ring behind, the oldest are
 * gone. sent - head - unmatched is how many reports have not come back.
 * Older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_AWDL_AFSTAT  "rtlRadioAwdlAfStat" /* p1: struct rtl_radio_awdl_afstat * (out) */
#define RTL_RADIO_AFSTAT_RING     16
struct rtl_radio_awdl_afstat {
	uint32_t head;             /* results written since the kext loaded */
	uint32_t sent;             /* frames that asked for a report */
	uint32_t unmatched;        /* reports whose number no frame was waiting for */
	uint8_t  raw[8];           /* payload of the last report of either kind */
	struct {
		uint8_t mac[6];        /* receiver of the frame */
		uint8_t status;        /* 0 acknowledged, 1 retry limit or lifetime reached */
		uint8_t retries;       /* data retries the chip counted */
	} e[RTL_RADIO_AFSTAT_RING];
};

/*
 * Parks the radio on one channel at 20 MHz while it is not associated, so
 * AWDL can sit on a social channel without a network there. Queued behind
 * any command already pending; later scans come back to it. kIOReturnBusy
 * while linked, joining or scanning, kIOReturnBadArgument for a channel
 * outside the channel plan. rtl_radio_status.idle_channel shows where the
 * radio sits. Older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_SET_CHANNEL  "rtlRadioSetChannel" /* p1: const uint32_t *channel */
/*
 * While associated, moves the radio to another channel for an AWDL slot, or
 * back to the BSS with channel 0. Leaving sends a null frame with PM set and
 * holds the data queues; returning sends PM clear. Inside the BSS's 40 or
 * 80 MHz block only the primary sub-channel moves; elsewhere it goes at
 * 20 MHz. Synchronous (USB register writes): not from inside a radio hook.
 * rtl_radio_status keeps reporting the BSS channel. kIOReturnBusy unless
 * linked with no join running and either no scan or a scan in its back-op
 * (RTL_RADIO_SCAN_BACKOP), which then waits for channel 0 before going on;
 * older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_AWDL_OFFCH   "rtlRadioAwdlOffch"  /* p1: const uint32_t *channel (0 = BSS) */

/*
 * A-MPDU for AWDL data. While a rate is set, a unicast AWDL frame that is QoS
 * data (subtype 8) keeps the sequence number its header carries, goes into
 * the AC queue of its TID at that rate and may be joined with the frames
 * behind it into an A-MPDU. The caller holds a Block Ack agreement with the
 * receiver for that TID and numbers the frames in order; other frames go as
 * before. rate is a TX descriptor index, HT 0x0c..0x1b or VHT one or two
 * streams 0x2c..0x3f, and 0 turns it off. bw is the widest RTL_RADIO_BW_* to
 * use: the frame goes no wider than the radio is tuned, which radio_bw
 * returns. Lasts until changed or the kext unloads. Older builds answer
 * kIOReturnUnsupported, and with them QoS data must not be sent.
 */
#define RTL_RADIO_FN_AWDL_AGG     "rtlRadioAwdlAgg"    /* p1: struct rtl_radio_awdl_agg * (in, out) */
struct rtl_radio_awdl_agg {
	uint32_t rate;             /* TX descriptor rate index, 0 = off */
	uint32_t bw;               /* widest RTL_RADIO_BW_* for these frames */
	uint32_t sgi;              /* short guard interval */
	uint32_t max_agg;          /* most MPDUs in an A-MPDU, 1..31; 0 = 31 */
	uint32_t density;          /* A-MPDU density code 0..7 (the receiver's minimum MPDU start spacing) */
	uint32_t radio_bw;         /* out: the width the radio is tuned to now */
};
/*
 * The width the radio has on a channel in an AWDL slot there, from the
 * decisions that tune it: while associated the BSS width on the BSS channel
 * and, elsewhere, what rtlRadioAwdlOffch takes (the BSS width inside its 40 or
 * 80 MHz block or in another 5 GHz block where that width is valid, 20 MHz
 * otherwise); while joining 20 MHz; otherwise the park of rtlRadioSetChannel
 * (rtl_awdl_park_bw on 5 GHz, 20 MHz on 2.4 GHz), and no wider than the radio
 * is now when it already sits on the channel, since no park moves it then.
 * *mhz is 20, 40 or 80; *upper, if given, is 1 when the channel is the upper
 * 20 MHz of its 40 MHz pair (secondary below), else 0. Reads driver state
 * only: no lock, no USB. kIOReturnBadArgument for a channel outside the plan;
 * older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_AWDL_WIDTH   "rtlRadioAwdlWidth"  /* p1: const uint32_t *channel, p2: uint32_t *mhz (out),
                                                        * p3: uint32_t *upper (out) or NULL */
/*
 * The number of AWDL peers with a data session, for the radio's status LED
 * (StatusLed.h): the layer gives it when it changes, and the radio takes 0
 * again when port 1 or the AWDL hook goes off. Reads and writes no register.
 * Older builds answer kIOReturnUnsupported.
 */
#define RTL_RADIO_FN_AWDL_PEERS   "rtlRadioAwdlPeers"  /* p1: const uint32_t *peers */
/* rtl_radio_status.state */
#define RTL_RADIO_S_IDLE          0
#define RTL_RADIO_S_SCAN          1
#define RTL_RADIO_S_JOINING       2   /* auth/assoc in progress */
#define RTL_RADIO_S_LINKED        3   /* associated (keys may still be pending, see port_valid) */

/* rtl_radio_status.scanning, non-zero while a scan runs */
#define RTL_RADIO_SCAN_BACKOP     2   /* back on its home channel between channels: the BSS when
                                       * linked, else the parked channel (idle_channel), while
                                       * an AWDL peer is around */

#define RTL_RADIO_BAND_2G         0
#define RTL_RADIO_BAND_5G         1

/* Channel widths (bss_bw, rx_bw) */
#define RTL_RADIO_BW_20           0
#define RTL_RADIO_BW_40           1
#define RTL_RADIO_BW_80           2

#define RTL_RADIO_MAX_IES         1024

struct rtl_radio_bss {
	uint8_t  bssid[6];
	uint8_t  ssid_len;
	uint8_t  ssid[32];
	uint8_t  channel;          /* primary channel */
	uint8_t  band;             /* RTL_RADIO_BAND_* */
	uint8_t  security;         /* 0 = open, 1 = WEP, 2 = WPA, 3 = WPA2 */
	int16_t  rssi_dbm;
	uint16_t capinfo;
	uint16_t intval;           /* beacon interval (TU) */
	uint16_t ies_len;          /* bytes valid in ies[] */
	uint8_t  ies[RTL_RADIO_MAX_IES]; /* elements from the SSID on, as received
	                            * (truncated past RTL_RADIO_MAX_IES) */
};

struct rtl_radio_chan {
	uint8_t  channel;
	uint8_t  band;             /* RTL_RADIO_BAND_* */
	uint8_t  passive;          /* passive scan only (DFS / regulatory) */
	uint8_t  reserved;
};

struct rtl_radio_join {
	uint8_t  ssid_len;
	uint8_t  ssid[32];
	uint8_t  has_pmk;          /* 0 = open network */
	uint8_t  pmk[32];          /* WPA2-PSK PMK (PBKDF2 output), never a passphrase */
	uint8_t  bssid[6];         /* preferred BSS, all zero = any */
	uint8_t  is_sae;           /* SAE join: password below, not a PMK */
	uint8_t  sae_password_len;
	uint8_t  sae_password[RTL_RADIO_SAE_PASSWORD_MAX]; /* consumed and wiped by the radio */
};

struct rtl_radio_status {
	uint32_t state;            /* RTL_RADIO_S_* */
	uint8_t  port_valid;       /* 4-way handshake done (or open network) */
	uint8_t  rsn;              /* WPA2 in use */
	uint8_t  bssid[6];
	uint8_t  ssid_len;
	uint8_t  ssid[32];
	uint8_t  channel;          /* primary channel */
	uint8_t  band;             /* RTL_RADIO_BAND_* */
	uint8_t  bss_bw;           /* operating width, RTL_RADIO_BW_* */
	uint8_t  center;           /* center channel of that width */
	int16_t  rssi_dbm;
	uint8_t  tx_rate;          /* DESC_RATE* from the FW RA report (0x2C + n = VHT1SS MCSn, ...) */
	uint8_t  tx_sgi;
	uint8_t  rx_rate;          /* DESC_RATE* of the last unicast data frame */
	uint8_t  rx_bw;
	uint8_t  rx_sgi;
	uint8_t  nss;              /* spatial streams negotiated with the AP (1-4) */
	uint8_t  hook_attached;    /* RX goes to the data hook, en2 link inactive */
	uint8_t  scanning;         /* a scan is running (state stays LINKED during a
	                            * scan while associated); 0 from builds before 4.11 */
	int8_t   noise_dbm;        /* noise floor estimate, 0 = unknown (and before 4.16) */
	uint8_t  idle_channel;     /* channel the radio sits on while IDLE, and in a
	                            * scan's return to it (RTL_RADIO_S_SCAN with
	                            * RTL_RADIO_SCAN_BACKOP); 0 in the other states
	                            * and from builds without rtlRadioSetChannel */
};

#endif /* _RTL8814_RADIO_ABI_H */
