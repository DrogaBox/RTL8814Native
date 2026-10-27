/*
 * RTL8814Native — wireless.h shim for XNU
 *
 * Provides wireless extensions structs used by the Realtek driver.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_WIRELESS_H_RTL8814
#define _LINUX_WIRELESS_H_RTL8814

#include <linux/types.h>

#define IFNAMSIZ 16

struct iw_point {
	void *pointer;
	__u16 length;
	__u16 flags;
};

union iwreq_data {
	char name[IFNAMSIZ];
	struct iw_point data;
};

struct iwreq {
	union {
		char ifrn_name[IFNAMSIZ];
	} ifr_ifrn;
	union iwreq_data u;
};

struct iw_quality {
	__u8 qual;
	__u8 level;
	__u8 noise;
	__u8 updated;
};

struct iw_discarded {
	__u32 nwid;
	__u32 code;
	__u32 fragment;
	__u32 retries;
	__u32 misc;
};

struct iw_missed {
	__u32 beacon;
};

struct iw_statistics {
	__u16 status;
	struct iw_quality qual;
	struct iw_discarded discard;
	struct iw_missed miss;
};

#define PKTHDR_RADIOTAP_VERSION 0

enum {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,
	IEEE80211_RADIOTAP_MCS = 19,
	IEEE80211_RADIOTAP_AMPDU_STATUS = 20,
	IEEE80211_RADIOTAP_VHT = 21,
	IEEE80211_RADIOTAP_TIMESTAMP = 22,
	IEEE80211_RADIOTAP_HE = 23,
	IEEE80211_RADIOTAP_HE_MU = 24,
};

#define IEEE80211_RADIOTAP_F_CFP        0x01
#define IEEE80211_RADIOTAP_F_SHORTPRE   0x02
#define IEEE80211_RADIOTAP_F_WEP        0x04
#define IEEE80211_RADIOTAP_F_FRAG       0x08
#define IEEE80211_RADIOTAP_F_FCS        0x10
#define IEEE80211_RADIOTAP_F_DATAPAD    0x20
#define IEEE80211_RADIOTAP_F_BADFCS     0x40
#define IEEE80211_RADIOTAP_F_SHORTGI    0x80

#define IEEE80211_CHAN_2GHZ             0x0080
#define IEEE80211_CHAN_5GHZ             0x0100
#define IEEE80211_CHAN_OFDM             0x0040
#define IEEE80211_CHAN_CCK              0x0020
#define IEEE80211_CHAN_DYN              0x0400

struct ieee80211_radiotap_header {
	__u8 it_version;
	__u8 it_pad;
	__le16 it_len;
	__le32 it_present;
} __attribute__((packed));

static inline __u16 ieee80211_get_radiotap_len(const __u8 *buf)
{
	const struct ieee80211_radiotap_header *r = (const struct ieee80211_radiotap_header *)buf;
	return (__u16)r->it_len;
}

#endif /* _LINUX_WIRELESS_H_RTL8814 */
