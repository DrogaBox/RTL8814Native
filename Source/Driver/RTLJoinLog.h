/*
 * Join event log: the MLME / WPA2 steps of the last joins and leaves, read
 * through kRTLUC_GetJoinLog (Tools/test_uc joinlog). Shared with test_uc.
 */
#ifndef RTL_JOIN_LOG_H
#define RTL_JOIN_LOG_H

#include <stdint.h>

#define RTL_JLOG_N 96

enum {
	JL_RADIO_JOIN = 1,      /* a: BSSID given, b: fw_state */
	JL_RADIO_LEAVE,         /* a: fw_state */
	JL_DO_JOIN,             /* a: fw_state, b: scanned queue empty */
	JL_SELECT,              /* a: rtw_select_and_join_from_scanned_queue result */
	JL_SURVEY_FOR_JOIN,     /* a: rtw_sitesurvey_cmd result, b: directed probe channel */
	JL_CLNT_JOIN,           /* a: channel, b: bandwidth */
	JL_AUTH_RX,             /* a: seq, b: status */
	JL_ASSOC_RX,            /* a: status */
	JL_DEAUTH_RX,           /* a: reason */
	JL_DISASSOC_RX,         /* a: reason */
	JL_LINK_TIMER,          /* a: mlmeinfo state, b: reauth/reassoc count */
	JL_JOIN_RES,            /* a: aid_res (<0 = failure), b: status */
	JL_JOINBSS_EVT,         /* a: join_res, b: status */
	JL_JOIN_TIMEOUT,        /* a: fw_state */
	JL_DISCONNECT_HDL,      /* a: was associated */
	JL_INDICATE_DISC,       /* a: reason, b: locally generated */
	JL_WPA2,                /* a: new WPA2 state */
	JL_SURVEY_DONE,         /* a: fw_state, b: to_join */
	JL_AUTH_TX,             /* a: auth seq, b: oper channel | bw << 8 */
	JL_MGMT_RX,             /* from the join BSSID while authenticating/associating:
	                         * a: subtype | A1 is us << 8, b: mlmeinfo state */
	JL_REGS,                /* at each link timer: a: TXPAUSE | MSR << 8 | beacons from
	                         * the BSSID since clnt join << 16, b: RCR */
	JL_FLUSH_SCANQ,         /* a: ms since the last leave, b: 0 */
	JL_SAE_AUTH_RX,         /* a: SAE transaction sequence, b: exact status */
	JL_SAE_AUTH_TX,         /* a: SAE transaction sequence, b: status */
	JL_SAE_STEP,            /* a: SAE step (RTL_SAE_STEP_*), b: result or status */
	JL_PMF,                 /* a: RTL_PMF_* kind, b: detail (reason, key id or subtype) */
	JL_CSA,                 /* a: RTL_CSA_* kind, b: channel | bw << 8 | count << 16 */
	JL_BA,                  /* Block Ack action from the AP. a: action | tid << 8 | initiator << 16,
	                         * b: ADDBA req: buffer size | timeout << 16; DELBA: reason; ADDBA resp: status */
};

enum {
	RTL_PMF_IGTK = 1,       /* IGTK installed, b: key id */
	RTL_PMF_DEC_OK,         /* protected unicast frame verified, b: subtype | reason << 8 */
	RTL_PMF_DEC_FAIL,       /* protected unicast frame failed CCMP, b: subtype */
	RTL_PMF_REPLAY,         /* unicast PN or IPN not newer, b: subtype */
	RTL_PMF_UNPROT_DROP,    /* unprotected deauth/disassoc dropped, b: subtype | reason << 8 */
	RTL_PMF_BIP_OK,         /* group frame verified, b: subtype */
	RTL_PMF_BIP_FAIL,       /* group frame failed BIP or has no MME, b: subtype */
	RTL_PMF_TX_PROTECTED,   /* a deauth/disassoc/robust action went out protected, b: subtype | category << 8 */
	RTL_PMF_SA_QUERY        /* answered an SA Query request, b: transaction id */
};

enum {
	RTL_CSA_SEEN = 1,       /* the AP announced a channel switch */
	RTL_CSA_FOLLOW,         /* switched to the announced channel without leaving */
	RTL_CSA_IGNORED         /* announcement not followed (bad channel or already there) */
};

struct rtl_jlog_ev {
	uint32_t t_ms;
	uint16_t ev;
	uint16_t pad;
	int32_t  a, b;
};

struct rtl_jlog {
	uint32_t next;          /* events logged so far; slot = index % RTL_JLOG_N */
	uint32_t pad;
	struct rtl_jlog_ev e[RTL_JLOG_N];
};

#endif
