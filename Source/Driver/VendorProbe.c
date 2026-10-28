/*
 * RTL8814Native — Phase 1 Vendor Probe & Bring-Up
 *
 * Implements hardware radio initialization, register I/O smoke testing,
 * chip ID detection, EFUSE MAC reading, and firmware download.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */

#include <drv_types.h>
#include <usb_ops.h>
#include <hal_data.h>
#include <hal_intf.h>
#include <linux/workqueue.h>
#include "VendorGlue.hpp"
#include "SAEJoin.h"
#include "WPA2Supplicant.h"
#include "RTLRadioABI.h"

extern struct rtl8814_rx_log g_rx_log;
extern struct rtl_jlog g_jlog;

void rtl8814native_get_join_log(struct rtl_jlog *out)
{
	_rtw_memcpy(out, &g_jlog, sizeof(*out));
}
extern volatile uint32_t g_rx_reassemble;
extern volatile uint32_t g_rx_skb_recycle;
static uint32_t g_bulk_in_mps;
static uint32_t g_usb_switch;   /* RTL_USB_SWITCH_*; survives re-probe, reset on kext load */

extern netdev_tx_t _rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev);

/* Active adapter & dvobj instances */
static struct dvobj_priv *g_dvobj = NULL;
static _adapter *g_padapter = NULL;
static struct rtl8814_radio_telemetry g_radio_telem = {0};

_adapter *rtl8814native_get_primary_adapter(void)
{
	return g_padapter;
}
static struct sk_buff_head g_tx_skb_queue;
static struct work_struct g_tx_work;
static int g_tx_work_inited = 0;

/* Sleep/wake: g_suspended spans setPowerState(0)..(1); the last successful
 * join credentials drive the automatic re-association at wake. Kext
 * lifetime, so a re-enumerated device could also reuse them later. */
static volatile int g_suspended;
static char g_last_ssid[33];
static char g_last_psk[65];
static uint32_t g_last_auth;

/* The AP ignores our auth for a while after a leave (joinlog 4.19/4.20: even a
 * directed one-channel probe gets no answer in the first ~15 s; the full
 * directed SSID survey or ~20 s of quiet are what works). Joins within this
 * window of a leave flush the scan queue to take the survey path. */
#define RTL_JOIN_REPROBE_MS 25000
static volatile systime g_last_leave_ms;

static void rtl8814_tx_work_fn(struct work_struct *w)
{
	(void)w;
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&g_tx_skb_queue)) != NULL) {
		if (!g_padapter || !g_padapter->bup || RTW_CANNOT_TX(g_padapter)) {
			dev_kfree_skb_any(skb);
			continue;
		}
		_rtw_xmit_entry(skb, g_padapter->pnetdev);
	}
}

void rtl8814native_get_radio_telemetry(struct rtl8814_radio_telemetry *out)
{
	if (out)
		_rtw_memcpy(out, &g_radio_telem, sizeof(struct rtl8814_radio_telemetry));
}

u8 rtw_init_default_value(_adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv     *pxmitpriv = &padapter->xmitpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	psecuritypriv->binstallGrpkey = _FAIL;
	psecuritypriv->sw_encrypt = pregistrypriv->software_encrypt;
	psecuritypriv->sw_decrypt = pregistrypriv->software_decrypt;

	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot11PrivacyKeyIndex = 0;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpKeyid = 1;

	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
	psecuritypriv->ndisencryptstatus = Ndis802_11WEPDisabled;

	rtw_init_registrypriv_dev_network(padapter);
	rtw_update_registrypriv_dev_network(padapter);

	rtw_hal_def_value_init(padapter);

	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->bLinkInfoDump = 0;
	padapter->bNotifyChannelChange = _FALSE;

	/* Linux os_intfs.c "for debug purpose" defaults. 0xFF / *_INVALID mean
	 * "driver/HAL default"; left at 0, driver_tx_bw_mode caps TX at 20 MHz and
	 * driver_rx_ampdu_factor advertises an 8 KB HT A-MPDU limit. */
	padapter->fix_rate = 0xFF;
	padapter->data_fb = 0;
	padapter->fix_bw = 0xFF;
	padapter->power_offset = 0;
	padapter->rsvd_page_offset = 0;
	padapter->rsvd_page_num = 0;
#ifdef CONFIG_AP_MODE
	padapter->bmc_tx_rate = pregistrypriv->bmc_tx_rate;
#endif
	padapter->driver_tx_bw_mode = pregistrypriv->tx_bw_mode;
	padapter->driver_ampdu_spacing = 0xFF;
	padapter->driver_rx_ampdu_factor = 0xFF;
	padapter->driver_rx_ampdu_spacing = 0xFF;
	padapter->fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	padapter->fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID;
#ifdef CONFIG_TX_AMSDU
	padapter->tx_amsdu = 2;
	padapter->tx_amsdu_rate = 400;
#endif
	padapter->driver_tx_max_agg_num = 0xFF;

	return _SUCCESS;
}

u8 rtw_init_drv_sw(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);

		dvobj->macid_ctl.num = rtw_min(hal_spec->macid_num, MACID_NUM_SW_LIMIT);
		dvobj->cam_ctl.sec_cap = hal_spec->sec_cap;
		dvobj->cam_ctl.num = rtw_min(hal_spec->sec_cam_ent_num, SEC_CAM_ENT_NUM_SW_LIMIT);
	}

	if (rtw_init_default_value(padapter) == _FAIL) {
		return _FAIL;
	}

	if (rtw_init_cmd_priv(&padapter->cmdpriv) == _FAIL) {
		return _FAIL;
	}
	padapter->cmdpriv.padapter = padapter;

	if (rtw_init_evt_priv(&padapter->evtpriv) == _FAIL) {
		return _FAIL;
	}

	if (is_primary_adapter(padapter))
		rtw_rfctl_init(padapter);

	if (is_primary_adapter(padapter)) {
		if (rtw_hal_rfpath_init(padapter) == _FAIL) {
			return _FAIL;
		}
		if (rtw_hal_trxnss_init(padapter) == _FAIL) {
			return _FAIL;
		}
	}

	if (rtw_init_mlme_priv(padapter) == _FAIL) {
		return _FAIL;
	}

#ifdef CONFIG_P2P
	/* Sets wdinfo.padapter: issue_assocreq -> rtw_append_assoc_req_wfd_ie
	 * dereferences it on every association (NULL+0x36b0 panic otherwise). */
	rtw_init_wifidirect_timers(padapter);
	init_wifidirect_info(padapter, P2P_ROLE_DISABLE);
	reset_global_wifidirect_info(padapter);
#ifdef CONFIG_WFD
	if (rtw_init_wifi_display_info(padapter) == _FAIL)
		rtl_printk("RTL8814Native: rtw_init_wifi_display_info failed\n");
#endif
#endif /* CONFIG_P2P */

	if (init_mlme_ext_priv(padapter) == _FAIL) {
		return _FAIL;
	}

#ifdef CONFIG_TDLS
	if (rtw_init_tdls_info(padapter) == _FAIL) {
		return _FAIL;
	}
#endif

	if (_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _FAIL) {
		return _FAIL;
	}

	if (_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL) {
		return _FAIL;
	}

	_rtw_spinlock_init(&padapter->security_key_mutex);

	if (_rtw_init_sta_priv(&padapter->stapriv) == _FAIL) {
		return _FAIL;
	}

	padapter->setband = WIFI_FREQUENCY_BAND_AUTO;
	padapter->fix_rate = 0xFF;
	padapter->power_offset = 0;
	padapter->rsvd_page_offset = 0;
	padapter->rsvd_page_num = 0;
	padapter->data_fb = 0;
	padapter->fix_rx_ampdu_accept = RX_AMPDU_ACCEPT_INVALID;
	padapter->fix_rx_ampdu_size = RX_AMPDU_SIZE_INVALID;

	rtw_init_bcmc_stainfo(padapter);
	rtw_init_pwrctrl_priv(padapter);
	rtw_hal_dm_init(padapter);

#ifdef DBG_CONFIG_ERROR_DETECT
	rtw_hal_sreset_init(padapter);
#endif

#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	rtw_bf_init(padapter);
#endif
#endif

#ifdef CONFIG_RTW_80211K
	rtw_init_rm(padapter);
#endif

	return _SUCCESS;
}

u8 rtw_free_drv_sw(_adapter *padapter)
{
	if (!padapter)
		return _SUCCESS;

	rtw_hal_dm_deinit(padapter);
	_rtw_free_sta_priv(&padapter->stapriv);
	_rtw_free_recv_priv(&padapter->recvpriv);
	_rtw_free_xmit_priv(&padapter->xmitpriv);
	free_mlme_ext_priv(&padapter->mlmeextpriv);
	rtw_free_mlme_priv(&padapter->mlmepriv);
	rtw_free_evt_priv(&padapter->evtpriv);
	rtw_free_cmd_priv(&padapter->cmdpriv);

	return _SUCCESS;
}

uint loadparam(_adapter *padapter)
{
	struct registry_priv *registry_par = &padapter->registrypriv;

	registry_par->chip_version = 0x00;
	registry_par->rfintfs = HWPI;
	registry_par->lbkmode = 0;
	registry_par->network_mode = Ndis802_11Infrastructure;

	_rtw_memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;

	registry_par->channel = 1;
	registry_par->wireless_mode = WIRELESS_MODE_MAX;

	registry_par->vrtl_carrier_sense = AUTO_VCS;
	registry_par->vcs_type = RTS_CTS;
	registry_par->rts_thresh = 2347;
	registry_par->frag_thresh = 2346;
	registry_par->preamble = PREAMBLE_LONG;
	registry_par->scan_mode = 1;
	registry_par->smart_ps = 2;
	registry_par->check_fw_ps = 1;
	registry_par->power_mgnt = PS_MODE_ACTIVE;
	registry_par->ips_mode = IPS_NONE;
	registry_par->radio_enable = 1;
	registry_par->long_retry_lmt = 7;
	registry_par->short_retry_lmt = 7;
	registry_par->busy_thresh = 40;
	registry_par->ack_policy = NORMAL_ACK;
	registry_par->mp_mode = 0;
	registry_par->usb_rxagg_mode = 2; /* RX_AGG_USB */
	registry_par->dynamic_agg_enable = 1;
	registry_par->wmm_enable = 1;
	registry_par->RegPwrTrimEnable = 2;
	registry_par->tx_bw_mode = 0x21;

	/* 802.11n / 802.11ac settings */
	registry_par->ht_enable = 1;
	registry_par->bw_mode = 0x21; /* 2.4G 40MHz, 5G 80MHz */
	registry_par->ampdu_enable = 1;
	registry_par->rx_stbc = 1;
	registry_par->tx_ampdu_amsdu = 2;
	registry_par->short_gi = 0x0f;
	registry_par->ldpc_cap = 0x33;
	registry_par->stbc_cap = 0x13;
	registry_par->beamform_cap = BIT(1) | BIT(3);
	_rtw_memset(registry_par->rx_ampdu_sz_limit_by_nss_bw, 0xFF,
	            sizeof(registry_par->rx_ampdu_sz_limit_by_nss_bw));

	registry_par->vht_enable = 1;
	registry_par->ampdu_factor = 7;
	registry_par->vht_rx_mcs_map[0] = 0xaa;
	registry_par->vht_rx_mcs_map[1] = 0xaa;

	/* Realtek RF, RFE, Channel Plan & PhyDM/HALRF Abilities */
	registry_par->lowrate_two_xmit = 1;
	registry_par->rf_path = RF_TYPE_MAX;
	registry_par->tx_nss = 0;
	registry_par->rx_nss = 0;
	_rtw_memset(registry_par->alpha2, 0xFF, 2);
	registry_par->channel_plan = CONFIG_RTW_CHPLAN;
	registry_par->bAcceptAddbaReq = _TRUE;
	registry_par->antdiv_cfg = 2;
	registry_par->antdiv_type = 0;
	registry_par->drv_ant_band_switch = 1;
	registry_par->hwpdn_mode = 2;
	registry_par->hw_wps_pbc = 1;
	registry_par->max_roaming_times = 2;
	registry_par->pll_ref_clk_sel = CONFIG_RTW_PLL_REF_CLK_SEL;
	registry_par->RegEnableTxPowerLimit = CONFIG_TXPWR_LIMIT_EN;
	registry_par->RegEnableTxPowerByRate = CONFIG_TXPWR_BY_RATE_EN;
	registry_par->TxBBSwing_2G = (s8)0xFF;
	registry_par->TxBBSwing_5G = (s8)0xFF;
	registry_par->bEn_RFE = 1;
	registry_par->RFE_Type = CONFIG_RTW_RFE_TYPE; /* 64 -> auto from EFUSE / 8814AU=1 */
	registry_par->PowerTracking_Type = 64;
	registry_par->AmplifierType_2G = 0;
	registry_par->AmplifierType_5G = 0;
	registry_par->GLNA_Type = 0;
	registry_par->phydm_ability = 0xffffffff;
	registry_par->halrf_ability = 0xffffffff;
	registry_par->switch_usb_mode = 1; /* U2 -> U3 (hal HW_VAR_USB_MODE) */

	return _SUCCESS;
}

struct dvobj_priv *devobj_init(void)
{
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)rtw_zmalloc(sizeof(*pdvobj));
	if (!pdvobj)
		return NULL;

	_rtw_mutex_init(&pdvobj->hw_init_mutex);
	_rtw_mutex_init(&pdvobj->h2c_fwcmd_mutex);
	_rtw_mutex_init(&pdvobj->setch_mutex);
	_rtw_mutex_init(&pdvobj->setbw_mutex);
	_rtw_mutex_init(&pdvobj->rf_read_reg_mutex);
	_rtw_mutex_init(&pdvobj->ioctrl_mutex);

	pdvobj->processing_dev_remove = _FALSE;
	ATOMIC_SET(&pdvobj->disable_func, 0);

	rtw_macid_ctl_init(&pdvobj->macid_ctl);
	_rtw_spinlock_init(&pdvobj->cam_ctl.lock);
	_rtw_mutex_init(&pdvobj->cam_ctl.sec_cam_access_mutex);

	rtw_init_timer(&(pdvobj->dynamic_chk_timer), NULL, rtw_dynamic_check_timer_handlder, pdvobj);
	rtw_init_timer(&(pdvobj->periodic_tsf_update_end_timer), NULL, rtw_hal_periodic_tsf_update_end_timer_hdl, pdvobj);

	pdvobj->scan_deny = _FALSE;
	return pdvobj;
}

/* Undo steps 1-7 of vendor_probe (no drv_sw, threads or URBs yet). Used when
 * the chip is about to vanish for the USB 3.0 re-enumeration; the old device
 * is gone, so this must not touch the bus. */
static void rtl8814_free_partial_probe(void)
{
	if (g_padapter) {
		rtw_hal_free_data(g_padapter);
		rtw_vmfree((u8 *)g_padapter, sizeof(_adapter));
		g_padapter = NULL;
	}
	if (g_dvobj) {
		rtl_timer_destroy(&g_dvobj->dynamic_chk_timer.timer);
		rtl_timer_destroy(&g_dvobj->periodic_tsf_update_end_timer.timer);
		rtw_mfree((u8 *)g_dvobj, sizeof(*g_dvobj));
		g_dvobj = NULL;
	}
}

static u8 rtw_init_intf_priv(struct dvobj_priv *dvobj)
{
	(void)dvobj;
	return _SUCCESS;
}

int rtl8814native_vendor_probe(struct usb_device *udev, unsigned long driver_info)
{
	(void)driver_info;
	int status = 0;
	u32 val32 = 0;

	_rtw_memset(&g_radio_telem, 0, sizeof(g_radio_telem));
	g_radio_telem.probe_status = -1;

	rtl_printk("RTL8814Native: === Starting Vendor Radio Bring-Up (Phase 1) ===\n");

	if (!udev) {
		rtl_printk("RTL8814Native: udev is NULL\n");
		g_radio_telem.probe_status = -10;
		return -EINVAL;
	}

	/* 1. Allocate dvobj */
	g_dvobj = devobj_init();
	if (!g_dvobj) {
		rtl_printk("RTL8814Native: devobj_init failed\n");
		g_radio_telem.probe_status = -20;
		return -ENOMEM;
	}

	/* 2. Configure USB transport & Chip Identity on dvobj */
	g_dvobj->interface_type = RTW_USB;
	g_dvobj->chip_type = RTL8814A;
	g_dvobj->HardwareType = HARDWARE_TYPE_RTL8814AU;
	g_dvobj->pusbdev = udev;
	g_dvobj->usb_speed = RTW_USB_SPEED_2; /* refined below from the bulk IN wMaxPacketSize */
	_rtw_init_sema(&(g_dvobj->usb_suspend_sema), 0);
	rtw_reset_continual_io_error(g_dvobj);

	/* Discover all bulk endpoints from RTLUsbDevice */
	struct usb_endpoint_descriptor *in_ep = rtl_usb_find_bulk_in(udev);
	u8 out_eps[4] = {0};
	u8 num_out = rtl_usb_get_out_ep_info(udev, out_eps, 4);
	if (in_ep) {
		/* The 8814AU enumerates as USB 2.0 High-Speed (512-byte bulk) unless it
		 * has been mode-switched to USB 3.0 (1024). The HAL derives the bulk OUT
		 * ZLP padding, the RX DMA burst length and the RF path count from this,
		 * so it must match the real link. */
		u16 mps = in_ep->wMaxPacketSize & 0x7FF;
		g_bulk_in_mps = mps;
		if (mps >= 1024)
			g_dvobj->usb_speed = RTW_USB_SPEED_3;
		else if (mps >= 512)
			g_dvobj->usb_speed = RTW_USB_SPEED_2;
		else
			g_dvobj->usb_speed = RTW_USB_SPEED_1_1;
		rtl_printk("RTL8814Native: bulk in wMaxPacketSize=%u -> usb_speed=%d\n",
			   mps, g_dvobj->usb_speed);
	}
	if (in_ep && num_out > 0) {
		g_dvobj->RtInPipe[0] = in_ep->bEndpointAddress & 0x0F;
		g_dvobj->RtNumInPipes = 1;
		for (u8 i = 0; i < num_out && i < 4; i++)
			g_dvobj->RtOutPipe[i] = out_eps[i];
		g_dvobj->RtNumOutPipes = num_out;
		rtl_printk("RTL8814Native: bulk in ep: 0x%02x, %u bulk out eps: 0x%02x 0x%02x 0x%02x 0x%02x\n",
			   in_ep->bEndpointAddress, num_out,
			   out_eps[0], out_eps[1], out_eps[2], out_eps[3]);
	} else {
		rtl_printk("RTL8814Native: warning: bulk endpoints not found via helper\n");
		g_dvobj->RtInPipe[0] = 4;
		g_dvobj->RtNumInPipes = 1;
		g_dvobj->RtOutPipe[0] = 5;
		g_dvobj->RtOutPipe[1] = 6;
		g_dvobj->RtOutPipe[2] = 8;
		g_dvobj->RtNumOutPipes = 3;
	}

	rtw_init_intf_priv(g_dvobj);

	/* 3. Allocate primary adapter */
	g_padapter = (_adapter *)rtw_zvmalloc(sizeof(_adapter));
	if (!g_padapter) {
		rtl_printk("RTL8814Native: failed to allocate adapter\n");
		g_radio_telem.probe_status = -30;
		return -ENOMEM;
	}

	g_padapter->dvobj = g_dvobj;
	g_dvobj->padapters[0] = g_padapter;
	g_dvobj->iface_nums = 1;
	g_padapter->isprimary = _TRUE;
	g_padapter->adapter_type = PRIMARY_ADAPTER;
	g_padapter->iface_id = IFACE_ID0;
	g_padapter->hw_port = HW_PORT0;
	g_padapter->pnetdev = rtl8814native_get_netdev();
	rtw_clr_drv_stopped(g_padapter);
	rtw_clr_surprise_removed(g_padapter);

	loadparam(g_padapter);

	/* 4. Hook I/O operations (hal_usb.c) and HAL operations */
	if (rtw_init_io_priv(g_padapter, usb_set_intf_ops) == _FAIL) {
		rtl_printk("RTL8814Native: rtw_init_io_priv failed\n");
		g_radio_telem.probe_status = -40;
		return -EIO;
	}

	if (rtw_set_hal_ops(g_padapter) == _FAIL) {
		rtl_printk("RTL8814Native: rtw_set_hal_ops failed\n");
		g_radio_telem.probe_status = -50;
		return -EIO;
	}

	/* 5. Smoke test: read chip version register (REG_SYS_CFG = 0x00F0) */
	val32 = rtw_read32(g_padapter, REG_SYS_CFG);
	g_radio_telem.reg_sys_cfg = val32;
	rtl_printk("RTL8814Native: [Register I/O Smoke Test] REG_SYS_CFG = 0x%08X\n", val32);

	/* Read chip version into HalData */
	rtw_hal_read_chip_version(g_padapter);
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(g_padapter);
	if (pHalData) {
		g_radio_telem.chip_type = pHalData->version_id.ICType;
		g_radio_telem.cut_version = pHalData->version_id.CUTVersion;
		rtl_printk("RTL8814Native: Chip ICType = %d (8814A=%d), CUTVersion = %d\n",
			   pHalData->version_id.ICType, CHIP_8814A,
			   pHalData->version_id.CUTVersion);
	}

	/* 6. Chip configure (maps USB endpoints to TX/RX queues) */
	rtw_hal_chip_configure(g_padapter);

	/* 7. Read chip info (EFUSE permanent MAC address!) */
	rtl_printk("RTL8814Native: reading EFUSE / PROM adapter info...\n");
	status = rtw_hal_read_chip_info(g_padapter);
	if (status != _SUCCESS) {
		rtl_printk("RTL8814Native: rtw_hal_read_chip_info returned %d\n", status);
	}

	/* 7b. USB 2.0 -> 3.0 mode switch, as Linux usb_reprobe_switch_usb_mode()
	 * does right after reading the efuse. In HS the HAL arms the chip for SS
	 * and writes 0x05=0x80 (USB disconnect); the chip re-enumerates on the
	 * SuperSpeed lanes and a new kext instance probes it. In SS it clears
	 * the switch bits. ISSUED is kept for the kext's lifetime so that a chip
	 * which falls back to HS is brought up in HS instead of looping. */
	if (pHalData && pHalData->EEPROMUsbSwitch) {
		g_usb_switch |= RTL_USB_SWITCH_EFUSE_OK;
		if (IS_SUPER_SPEED_USB(g_padapter)) {
			u8 unused = _FALSE;
			rtw_hal_set_hwreg(g_padapter, HW_VAR_USB_MODE, &unused);
			g_usb_switch |= RTL_USB_SWITCH_SS_FINALIZED;
			rtl_printk("RTL8814Native: running in USB 3.0 mode\n");
		} else if (IS_HIGH_SPEED_USB(g_padapter) && !(g_usb_switch & RTL_USB_SWITCH_ISSUED)) {
			u8 disconnected = _FALSE;
			g_usb_switch |= RTL_USB_SWITCH_ISSUED;
			rtw_hal_set_hwreg(g_padapter, HW_VAR_USB_MODE, &disconnected);
			if (disconnected) {
				rtl_printk("RTL8814Native: switching to USB 3.0, device will re-enumerate\n");
				rtl8814_free_partial_probe();
				g_radio_telem.probe_status = RTL_PROBE_USB3_SWITCH;
				return RTL_PROBE_USB3_SWITCH;
			}
		}
	}

	/* 7c. A warm restart leaves the dongle powered (VBUS stays up) with the
	 * MAC/BB/RF still running from the previous session. The vendor power-on
	 * flow only logs "MAC has already power on" and then runs LCK/IQK on the
	 * live chip; measured after a macOS restart: ~40% of the AP's frames
	 * retransmitted and ~55 Mbit/s upload, vs ~3% and ~250 Mbit/s after a
	 * cold boot. Power the chip off first, as rtw88 does
	 * (rtw_mac_power_on: -EALREADY -> power off -> power on). */
	{
		u8 clkr = rtw_read8(g_padapter, REG_SYS_CLKR + 1);
		u8 cr = rtw_read8(g_padapter, REG_CR);
		u32 off = 0;

		if ((clkr & BIT3) && cr != 0 && cr != 0xEA) {
			rtw_hal_power_off(g_padapter);
			off = 1;
		}
		g_rate_diag.warm_start = clkr | ((u32)cr << 8) | (off << 16);
		rtl_printk("RTL8814Native: probe power state SYS_CLKR+1=0x%02x CR=0x%02x -> %s\n",
			   clkr, cr, off ? "warm start, chip powered off before HAL init" : "cold");
	}

	/* 8. Initialize core driver software structures */
	if (rtw_init_drv_sw(g_padapter) == _FAIL) {
		rtl_printk("RTL8814Native: rtw_init_drv_sw failed\n");
		g_radio_telem.probe_status = -60;
		return -ENODEV;
	}

	/* 9. Configure MAC address from EEPROM/EFUSE */
	rtw_macaddr_cfg(adapter_mac_addr(g_padapter), get_hal_mac_addr(g_padapter));
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mbid_camid_alloc(g_padapter, adapter_mac_addr(g_padapter));
#endif
	rtw_hal_set_hwreg(g_padapter, HW_VAR_MAC_ADDR, adapter_mac_addr(g_padapter));

	u8 *mac = adapter_mac_addr(g_padapter);
	if (mac && (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])) {
		rtl_printk("RTL8814Native: SUCCESS: Permanent hardware MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
			   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		_rtw_memcpy(g_radio_telem.mac, mac, ETH_ALEN);

		/* Update net_device with real hardware MAC address */
		struct net_device *ndev = rtl8814native_get_netdev();
		if (ndev) {
			_rtw_memcpy(ndev->dev_addr, mac, ETH_ALEN);
			_rtw_memcpy(ndev->perm_addr, mac, ETH_ALEN);
		}
	} else {
		rtl_printk("RTL8814Native: warning: permanent MAC is all zeros\n");
	}

	/* 10. Hardware initialization and Firmware Download */
	rtl_printk("RTL8814Native: initializing HAL and downloading firmware...\n");
	status = rtw_hal_init(g_padapter);
	g_radio_telem.hal_init_status = status;
	if (status == _SUCCESS) {
		g_radio_telem.fw_ready = (pHalData && pHalData->bFWReady) ? 1 : 0;
		rtl_printk("RTL8814Native: rtw_hal_init & firmware download SUCCEEDED! (fw_ready=%d)\n",
			   g_radio_telem.fw_ready);
	} else {
		rtl_printk("RTL8814Native: rtw_hal_init returned status %d\n", status);
	}

	/* 11. Start Driver Threads (RTW_CMD_THREAD) and USB Bulk IN RX IRPs */
	skb_queue_head_init(&g_tx_skb_queue);
	INIT_WORK(&g_tx_work, rtl8814_tx_work_fn);
	g_tx_work_inited = 1;

	if (rtw_start_drv_threads(g_padapter) == _FAIL) {
		rtl_printk("RTL8814Native: warning: rtw_start_drv_threads failed\n");
	} else {
		rtl_printk("RTL8814Native: RTW_CMD_THREAD started!\n");
	}

	rtw_intf_start(g_padapter);

	g_padapter->bup = _TRUE;
	g_padapter->net_closed = _FALSE;
	g_padapter->netif_up = _TRUE;

	_set_timer(&g_dvobj->dynamic_chk_timer, 2000);
	rtw_set_pwr_state_check_timer(adapter_to_pwrctl(g_padapter));

	g_radio_telem.probe_status = 0;
	rtl_printk("RTL8814Native: === Vendor Radio Bring-Up & RX/TX Pipeline Ready ===\n");
	return 0;
}

int rtl8814native_vendor_remove(void)
{
	rtl_printk("RTL8814Native: stopping vendor radio...\n");

	if (g_tx_work_inited) {
		cancel_work_sync(&g_tx_work);
		rtw_skb_queue_purge(&g_tx_skb_queue);
		g_tx_work_inited = 0;
	}

	if (g_padapter) {
		g_padapter->net_closed = _TRUE;
		g_padapter->bup = _FALSE;
		rtw_set_drv_stopped(g_padapter);

		rtw_intf_stop(g_padapter);
		rtw_stop_drv_threads(g_padapter);
		rtw_cancel_all_timer(g_padapter);

		/* Every schedule source is dead: URBs killed synchronously in
		 * intf_stop (killUrb waits for the completions), the cmd thread
		 * joined, timers cancelled. Drain the compat workqueues — their
		 * threads are kext-lifetime, so any entry still queued here would
		 * run later against the freed adapter below. */
		rtl_flush_scheduled_work();

		if (g_padapter->hal_func.hal_deinit)
			g_padapter->hal_func.hal_deinit(g_padapter);

		rtl_flush_scheduled_work();

		rtw_free_drv_sw(g_padapter);

		if (g_padapter->HalData) {
			rtw_vmfree((u8 *)g_padapter->HalData, sizeof(HAL_DATA_TYPE));
			g_padapter->HalData = NULL;
		}

		rtw_vmfree((u8 *)g_padapter, sizeof(_adapter));
		g_padapter = NULL;
	}

	if (g_dvobj) {
		rtw_mfree((u8 *)g_dvobj, sizeof(*g_dvobj));
		g_dvobj = NULL;
	}

	/* A stale instance removed while asleep (the dongle re-enumerated on
	 * wake) must not hand "suspended" to the next probe: its first sleep
	 * would skip the card disable and its wake would re-init a live chip. */
	g_suspended = 0;

	rtl_printk("RTL8814Native: vendor radio stopped and resources freed\n");
	return 0;
}

void rtl8814native_vendor_shutdown(void)
{
	if (!g_padapter || rtw_is_surprise_removed(g_padapter))
		return;

	rtl_printk("RTL8814Native: system shutdown, powering the radio off\n");

	/* Stop new TX and the watchdog before the HAL powers the chip down */
	g_padapter->net_closed = _TRUE;
	g_padapter->bup = _FALSE;
	rtw_set_drv_stopped(g_padapter);
	if (g_tx_work_inited) {
		cancel_work_sync(&g_tx_work);
		rtw_skb_queue_purge(&g_tx_skb_queue);
	}
	rtw_cancel_all_timer(g_padapter);

	/* As Linux rtw_dev_shutdown(): HAL deinit runs NIC_DISABLE_FLOW (card
	 * disable), then no more I/O to the chip */
	rtw_hal_deinit(g_padapter);
	rtw_set_surprise_removed(g_padapter);
	ATOMIC_SET(&g_dvobj->continual_io_error, MAX_CONTINUAL_IO_ERR + 1);
}

int rtl8814native_vendor_suspend(void)
{
	if (!g_padapter || rtw_is_surprise_removed(g_padapter))
		return -ENODEV;
	if (g_suspended)
		return 0;

	rtl_printk("RTL8814Native: system sleep, powering the radio off\n");

	/* The stop path of vendor_remove(), but every allocation stays: the
	 * wake path re-runs HAL init + firmware on the same _adapter. */
	rtl8814native_disconnect();

	g_padapter->net_closed = _TRUE;
	g_padapter->bup = _FALSE;
	rtw_set_drv_stopped(g_padapter);

	if (g_tx_work_inited) {
		cancel_work_sync(&g_tx_work);
		rtw_skb_queue_purge(&g_tx_skb_queue);
		g_tx_work_inited = 0;
	}

	rtw_intf_stop(g_padapter);
	rtw_stop_drv_threads(g_padapter);
	rtw_cancel_all_timer(g_padapter);

	rtw_hal_deinit(g_padapter);
	g_suspended = 1;
	return 0;
}

int rtl8814native_vendor_resume(void)
{
	int status;

	if (!g_padapter)
		return -ENODEV;
	if (!g_suspended)
		return 0;

	if (rtw_is_surprise_removed(g_padapter)) {
		/* The device vanished across sleep; IOKit tears this instance
		 * down and a fresh probe brings the re-enumerated one up. */
		g_suspended = 0;
		return -ENODEV;
	}

	rtl_printk("RTL8814Native: system wake, re-initializing the radio\n");

	/* Same warm-start guard as probe step 7c: calibrating on a live chip
	 * ruins the link (Milestone 4.4) */
	{
		u8 clkr = rtw_read8(g_padapter, REG_SYS_CLKR + 1);
		u8 cr = rtw_read8(g_padapter, REG_CR);
		if ((clkr & BIT3) && cr != 0 && cr != 0xEA)
			rtw_hal_power_off(g_padapter);
	}

	/* usb_write_port gates on drv_stopped, so clear it before the
	 * firmware download */
	rtw_clr_drv_stopped(g_padapter);
	rtl8814native_rx_stage_reset();

	status = rtw_hal_init(g_padapter);
	if (status != _SUCCESS) {
		rtl_printk("RTL8814Native: wake: rtw_hal_init failed (%d)\n", status);
		g_suspended = 0;
		rtw_set_surprise_removed(g_padapter);
		return -EIO;
	}

	skb_queue_head_init(&g_tx_skb_queue);
	INIT_WORK(&g_tx_work, rtl8814_tx_work_fn);
	g_tx_work_inited = 1;

	if (rtw_start_drv_threads(g_padapter) == _FAIL)
		rtl_printk("RTL8814Native: wake: rtw_start_drv_threads failed\n");

	rtw_intf_start(g_padapter);

	g_padapter->bup = _TRUE;
	g_padapter->net_closed = _FALSE;
	g_padapter->netif_up = _TRUE;
	_set_timer(&g_dvobj->dynamic_chk_timer, 2000);
	rtw_set_pwr_state_check_timer(adapter_to_pwrctl(g_padapter));

	g_suspended = 0;

	/* Re-associate with the network we were on at sleep. join_network is
	 * async (cmd thread + MLME + supplicant), so this just queues it. */
	rtl8814native_rejoin_last();

	return 0;
}

void rtl8814native_rejoin_last(void)
{
	if (g_last_ssid[0])
		rtl8814native_join_network(g_last_ssid,
					   g_last_psk[0] ? g_last_psk : NULL,
					   g_last_auth);
}

/* ======================================================================
 * Phase 2 & 3: 802.11 Scan, Join, Link Status, Disconnect & TX Entry
 * ====================================================================== */

int rtl8814native_trigger_scan(void)
{
	if (!g_padapter || !g_padapter->bup)
		return -ENODEV;

	/* Note: rtw_set_802_11_bssid_list_scan acquires &mlmepriv.lock internally */
	u8 res = rtw_set_802_11_bssid_list_scan(g_padapter, NULL);

	rtl_printk("RTL8814Native: triggered 802.11 scan (res=%u)\n", res);
	return (res == _TRUE) ? 0 : -EBUSY;
}

int rtl8814native_get_scan_results(struct rtl_scan_entry *entries, uint32_t max_entries, uint32_t *out_count)
{
	if (out_count)
		*out_count = 0;
	if (!g_padapter || !entries || max_entries == 0)
		return -EINVAL;

	struct mlme_priv *pmlmepriv = &g_padapter->mlmepriv;
	_queue *queue = &pmlmepriv->scanned_queue;
	_list *head, *plist;
	_irqL irqL;
	uint32_t count = 0;

	_enter_critical_bh(&queue->lock, &irqL);
	head = get_list_head(queue);
	plist = get_next(head);

	while (!rtw_end_of_queue_search(head, plist) && count < max_entries) {
		struct wlan_network *pnet = LIST_CONTAINOR(plist, struct wlan_network, list);
		plist = get_next(plist);

		WLAN_BSSID_EX *bss = &pnet->network;
		struct rtl_scan_entry *ent = &entries[count];
		_rtw_memset(ent, 0, sizeof(*ent));

		u32 ssid_len = bss->Ssid.SsidLength;
		if (ssid_len > 32)
			ssid_len = 32;
		if (ssid_len > 0)
			_rtw_memcpy(ent->ssid, bss->Ssid.Ssid, ssid_len);
		ent->ssid[ssid_len] = '\0';

		_rtw_memcpy(ent->bssid, bss->MacAddress, 6);
		ent->rssi = (int16_t)bss->Rssi;
		ent->channel = (uint8_t)bss->Configuration.DSConfig;

		/* Detect security from IEs and Privacy capability bit */
		uint8_t sec = 0;
		if (bss->Privacy)
			sec = 1; /* WEP / Privacy */
		if (bss->IELength > 12) {
			uint ie_len = 0;
			if (rtw_get_wpa2_ie(bss->IEs + 12, &ie_len, bss->IELength - 12))
				sec = 3; /* WPA2 */
			else if (rtw_get_wpa_ie(bss->IEs + 12, &ie_len, bss->IELength - 12))
				sec = 2; /* WPA */
		}
		ent->security = sec;
		count++;
	}
	_exit_critical_bh(&queue->lock, &irqL);

	if (out_count)
		*out_count = count;
	return 0;
}

int rtl8814native_join_network(const char *ssid_str, const char *psk_str, uint32_t auth_mode)
{
	if (!g_padapter || !g_padapter->bup || !ssid_str)
		return -ENODEV;

	u32 ssid_len = (u32)strlen(ssid_str);
	if (ssid_len == 0 || ssid_len > 32)
		return -EINVAL;

	rtl_sae_join_cancel(g_padapter);
	/* Configure Open or WPA2-PSK security + PMK + RSN IE */
	rtl8814_wpa2_configure(g_padapter, ssid_str, ssid_len, psk_str,
			       psk_str ? (u32)strlen(psk_str) : 0, auth_mode);

	NDIS_802_11_SSID ndis_ssid;
	_rtw_memset(&ndis_ssid, 0, sizeof(ndis_ssid));
	_rtw_memcpy(ndis_ssid.Ssid, (void *)ssid_str, ssid_len);
	ndis_ssid.SsidLength = ssid_len;

	rtw_set_802_11_infrastructure_mode(g_padapter, Ndis802_11Infrastructure, 0);
	u8 res = rtw_set_802_11_ssid(g_padapter, &ndis_ssid);

	if (res == _TRUE) {
		/* Remembered for the automatic re-join at wake */
		u32 plen = psk_str ? (u32)strlen(psk_str) : 0;
		if (plen >= sizeof(g_last_psk))
			plen = sizeof(g_last_psk) - 1;
		_rtw_memcpy(g_last_ssid, ssid_str, ssid_len);
		g_last_ssid[ssid_len] = '\0';
		if (plen) {
			_rtw_memcpy(g_last_psk, psk_str, plen);
			g_last_psk[plen] = '\0';
		} else {
			g_last_psk[0] = '\0';
		}
		g_last_auth = auth_mode;
	}

	rtl_printk("RTL8814Native: join_network('%s', auth=%u) -> res=%u\n",
		   ssid_str, auth_mode, res);
	return (res == _TRUE) ? 0 : -EIO;
}

int rtl8814native_disconnect(void)
{
	if (!g_padapter)
		return -ENODEV;

	rtl8814native_jlog(JL_RADIO_LEAVE, get_fwstate(&g_padapter->mlmepriv), 0);
	rtl_sae_join_cancel(g_padapter);
	g_last_leave_ms = rtw_get_current_time();
	rtl8814_wpa2_reset(g_padapter);
	rtw_set_802_11_disassociate(g_padapter);

	struct net_device *ndev = g_padapter->pnetdev ? g_padapter->pnetdev : rtl8814native_get_netdev();
	if (ndev)
		rtl_netif_carrier(ndev, 0);

	rtl_printk("RTL8814Native: disconnected\n");
	return 0;
}

static void rtl8814_fill_rate_diag(struct rtl8814_rate_diag *r);

/* ======================================================================
 * Radio ABI (RTLRadioABI.h): the surface a separate IO80211 layer kext
 * drives through RTL8814Native::callPlatformFunction.
 * ====================================================================== */

int rtl8814native_radio_scan_copy(struct rtl_radio_bss *out, uint32_t *count)
{
	if (!g_padapter || !out || !count)
		return -EINVAL;

	uint32_t cap = *count, n = 0;
	_queue *queue = &g_padapter->mlmepriv.scanned_queue;
	_list *head, *plist;
	_irqL irqL;

	_enter_critical_bh(&queue->lock, &irqL);
	head = get_list_head(queue);
	plist = get_next(head);
	while (!rtw_end_of_queue_search(head, plist) && n < cap) {
		struct wlan_network *pnet = LIST_CONTAINOR(plist, struct wlan_network, list);
		WLAN_BSSID_EX *bss = &pnet->network;
		struct rtl_radio_bss *b = &out[n];
		plist = get_next(plist);

		_rtw_memset(b, 0, sizeof(*b));
		_rtw_memcpy(b->bssid, bss->MacAddress, 6);
		b->ssid_len = (uint8_t)(bss->Ssid.SsidLength > 32 ? 32 : bss->Ssid.SsidLength);
		_rtw_memcpy(b->ssid, bss->Ssid.Ssid, b->ssid_len);
		b->channel = (uint8_t)bss->Configuration.DSConfig;
		b->band = b->channel > 14 ? RTL_RADIO_BAND_5G : RTL_RADIO_BAND_2G;
		b->rssi_dbm = (int16_t)bss->Rssi;
		if (bss->IELength >= _FIXED_IE_LENGTH_) {
			/* fixed part: timestamp 8, beacon interval 2, capability 2 */
			b->intval = (uint16_t)(bss->IEs[8] | (bss->IEs[9] << 8));
			b->capinfo = (uint16_t)(bss->IEs[10] | (bss->IEs[11] << 8));
			u32 ies = bss->IELength - _FIXED_IE_LENGTH_;
			if (ies > RTL_RADIO_MAX_IES)
				ies = RTL_RADIO_MAX_IES;
			_rtw_memcpy(b->ies, bss->IEs + _FIXED_IE_LENGTH_, ies);
			b->ies_len = (uint16_t)ies;
		}
		if (bss->Privacy)
			b->security = 1;
		if (bss->IELength > _FIXED_IE_LENGTH_) {
			uint ie_len = 0;
			if (rtw_get_wpa2_ie(bss->IEs + _FIXED_IE_LENGTH_, &ie_len, bss->IELength - _FIXED_IE_LENGTH_))
				b->security = 3;
			else if (rtw_get_wpa_ie(bss->IEs + _FIXED_IE_LENGTH_, &ie_len, bss->IELength - _FIXED_IE_LENGTH_))
				b->security = 2;
		}
		n++;
	}
	_exit_critical_bh(&queue->lock, &irqL);

	*count = n;
	return 0;
}

int rtl8814native_radio_channels(struct rtl_radio_chan *out, uint32_t *count)
{
	if (!g_padapter || !out || !count)
		return -EINVAL;

	struct rf_ctl_t *rfctl = adapter_to_rfctl(g_padapter);
	uint32_t cap = *count, n = 0, i;

	for (i = 0; i < rfctl->max_chan_nums && i < MAX_CHANNEL_NUM && n < cap; i++) {
		RT_CHANNEL_INFO *ch = &rfctl->channel_set[i];
		if (!ch->ChannelNum)
			continue;
		out[n].channel = ch->ChannelNum;
		out[n].band = ch->ChannelNum > 14 ? RTL_RADIO_BAND_5G : RTL_RADIO_BAND_2G;
		out[n].passive = (ch->ScanType == SCAN_PASSIVE) ? 1 : 0;
		out[n].reserved = 0;
		n++;
	}
	*count = n;
	return 0;
}

int rtl8814native_radio_join(struct rtl_radio_join *j)
{
	static const char hex[] = "0123456789abcdef";
	static const u8 zero_bssid[6];
	char ssid[33];
	char pmk_hex[65];
	int i, rc;

	if (!j)
		return -EINVAL;
	if (j->is_sae) {
		if (!j->ssid_len || j->ssid_len > 32 || j->has_pmk ||
		    !j->sae_password_len || j->sae_password_len > RTL_RADIO_SAE_PASSWORD_MAX) {
			_rtw_memset(j->sae_password, 0, sizeof(j->sae_password));
			j->sae_password_len = 0;
			return -EINVAL;
		}
		if (!g_padapter || !g_padapter->bup) {
			_rtw_memset(j->sae_password, 0, sizeof(j->sae_password));
			j->sae_password_len = 0;
			return -ENODEV;
		}
		{
			NDIS_802_11_SSID ndis_ssid;

			rtl8814native_jlog(JL_RADIO_JOIN, _rtw_memcmp((void *)j->bssid, (void *)zero_bssid, 6) != _TRUE,
					   get_fwstate(&g_padapter->mlmepriv));
			rtl_sae_join_cancel(g_padapter);
			rtl8814_wpa2_reset(g_padapter);
			rc = rtl_sae_join_prepare(g_padapter, j->sae_password, j->sae_password_len);
			_rtw_memset(j->sae_password, 0, sizeof(j->sae_password));
			j->sae_password_len = 0;
			if (rc)
				return rc;
			_rtw_memset(&ndis_ssid, 0, sizeof(ndis_ssid));
			_rtw_memcpy(ndis_ssid.Ssid, j->ssid, j->ssid_len);
			ndis_ssid.SsidLength = j->ssid_len;
			rtw_set_802_11_infrastructure_mode(g_padapter, Ndis802_11Infrastructure, 0);
			if (g_last_leave_ms && rtw_get_passing_time_ms(g_last_leave_ms) < RTL_JOIN_REPROBE_MS) {
				rtl8814native_jlog(JL_FLUSH_SCANQ, rtw_get_passing_time_ms(g_last_leave_ms), 0);
				g_join_flush_scanq = 1;
			}
			rc = rtw_set_802_11_connect(g_padapter, (u8 *)j->bssid, &ndis_ssid) == _TRUE ? 0 : -EIO;
			/* The passphrase is not kept, so a wake re-join must not run open */
			g_last_ssid[0] = '\0';
			if (rc)
				rtl_sae_join_cancel(g_padapter);
			return rc;
		}
	}
	if (j->sae_password_len) {
		_rtw_memset(j->sae_password, 0, sizeof(j->sae_password));
		j->sae_password_len = 0;
		return -EINVAL;
	}
	if (j->ssid_len == 0 || j->ssid_len > 32)
		return -EINVAL;
	if (!g_padapter || !g_padapter->bup)
		return -ENODEV;
	rtl_sae_join_cancel(g_padapter);
	rtl8814native_jlog(JL_RADIO_JOIN, _rtw_memcmp((void *)j->bssid, (void *)zero_bssid, 6) != _TRUE,
			   get_fwstate(&g_padapter->mlmepriv));

	_rtw_memcpy(ssid, j->ssid, j->ssid_len);
	ssid[j->ssid_len] = '\0';
	/* The supplicant takes a 64-hex-digit PSK as the PMK itself */
	if (j->has_pmk) {
		for (i = 0; i < 32; i++) {
			pmk_hex[i * 2] = hex[j->pmk[i] >> 4];
			pmk_hex[i * 2 + 1] = hex[j->pmk[i] & 0xF];
		}
		pmk_hex[64] = '\0';
	}

	if (_rtw_memcmp((void *)j->bssid, (void *)zero_bssid, 6) == _TRUE) {
		rc = rtl8814native_join_network(ssid, j->has_pmk ? pmk_hex : NULL, j->has_pmk ? 1 : 0);
	} else {
		NDIS_802_11_SSID ndis_ssid;

		rtl8814_wpa2_configure(g_padapter, ssid, j->ssid_len,
				       j->has_pmk ? pmk_hex : NULL, j->has_pmk ? 64 : 0,
				       j->has_pmk ? 1 : 0);
		_rtw_memset(&ndis_ssid, 0, sizeof(ndis_ssid));
		_rtw_memcpy(ndis_ssid.Ssid, j->ssid, j->ssid_len);
		ndis_ssid.SsidLength = j->ssid_len;
		rtw_set_802_11_infrastructure_mode(g_padapter, Ndis802_11Infrastructure, 0);
		if (g_last_leave_ms && rtw_get_passing_time_ms(g_last_leave_ms) < RTL_JOIN_REPROBE_MS) {
			rtl8814native_jlog(JL_FLUSH_SCANQ, rtw_get_passing_time_ms(g_last_leave_ms), 0);
			g_join_flush_scanq = 1;
		}
		rc = rtw_set_802_11_connect(g_padapter, (u8 *)j->bssid, &ndis_ssid) == _TRUE ? 0 : -EIO;
		if (rc == 0) {
			/* Remembered for the automatic re-join at wake */
			_rtw_memcpy(g_last_ssid, ssid, j->ssid_len + 1);
			if (j->has_pmk)
				_rtw_memcpy(g_last_psk, pmk_hex, sizeof(pmk_hex));
			else
				g_last_psk[0] = '\0';
			g_last_auth = j->has_pmk ? 1 : 0;
		}
	}
	_rtw_memset(pmk_hex, 0, sizeof(pmk_hex));
	return rc;
}

void rtl8814native_radio_status(struct rtl_radio_status *st)
{
	struct rtl8814_rate_diag r;

	if (!st)
		return;
	_rtw_memset(st, 0, sizeof(*st));
	if (!g_padapter)
		return;

	struct mlme_priv *pmlmepriv = &g_padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &g_padapter->mlmeextpriv;
	int wpa = rtl8814_wpa2_get_state();

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		st->state = RTL_RADIO_S_LINKED;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		st->state = RTL_RADIO_S_JOINING;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		st->state = RTL_RADIO_S_SCAN;
	else
		st->state = RTL_RADIO_S_IDLE;

	st->scanning = check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) ? 1 : 0;
	st->rsn = wpa != 0;
	st->port_valid = st->state == RTL_RADIO_S_LINKED && (!st->rsn || wpa == 3);
	if (st->state != RTL_RADIO_S_LINKED)
		return;

	WLAN_BSSID_EX *cur = &pmlmepriv->cur_network.network;
	_rtw_memcpy(st->bssid, cur->MacAddress, 6);
	st->ssid_len = (uint8_t)(cur->Ssid.SsidLength > 32 ? 32 : cur->Ssid.SsidLength);
	_rtw_memcpy(st->ssid, cur->Ssid.Ssid, st->ssid_len);
	st->rssi_dbm = (int16_t)cur->Rssi;
	st->channel = pmlmeext->cur_channel;
	st->band = st->channel > 14 ? RTL_RADIO_BAND_5G : RTL_RADIO_BAND_2G;
	st->bss_bw = pmlmeext->cur_bwmode;
	st->center = rtw_get_center_ch(pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);

	_rtw_memset(&r, 0, sizeof(r));
	rtl8814_fill_rate_diag(&r);
	st->tx_rate = r.tx_rate;
	st->tx_sgi = r.tx_sgi;
	st->rx_rate = r.last_rx_rate;
	st->rx_bw = r.last_rx_bw;
	st->rx_sgi = r.last_rx_sgi;
	{
		extern s32 g_rx_noise_q4;
		s32 q = g_rx_noise_q4;
		st->noise_dbm = q ? (int8_t)((q - 8) / 16) : 0;
	}
	/* VHT MCS map: 2 bits per stream, 3 = stream not supported */
	st->nss = 0;
	for (int k = 0; k < 4; k++) {
		u16 map = (u16)(r.sta_vht_map[0] | (r.sta_vht_map[1] << 8));
		if (((map >> (2 * k)) & 3) != 3)
			st->nss = (uint8_t)(k + 1);
	}
	if (!st->nss)
		st->nss = r.ap_ht_mcs[1] ? 2 : 1;
}

void rtl8814native_radio_mac(uint8_t mac[6])
{
	if (!mac)
		return;
	if (g_padapter)
		_rtw_memcpy(mac, adapter_mac_addr(g_padapter), 6);
	else
		_rtw_memset(mac, 0, 6);
}

/*
 * As rtw_net_set_mac_address: only while not linked or linking. The adapter
 * copy is what rtw_hal_init writes back to the register at resume.
 */
int rtl8814native_radio_set_mac(const uint8_t mac[6])
{
	struct mlme_priv *pmlmepriv;

	if (!mac)
		return -EINVAL;
	if (!g_padapter || !g_padapter->bup)
		return -ENODEV;
	if (_rtw_memcmp(adapter_mac_addr(g_padapter), (void *)mac, ETH_ALEN) == _TRUE)
		return 0;
	if (rtw_check_invalid_mac_address((u8 *)mac, _FALSE) == _TRUE)
		return -EINVAL;
	pmlmepriv = &g_padapter->mlmepriv;
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) || check_fwstate(pmlmepriv, _FW_LINKED))
		return -EBUSY;

	_rtw_memcpy(adapter_mac_addr(g_padapter), (void *)mac, ETH_ALEN);
	struct net_device *ndev = rtl8814native_get_netdev();
	if (ndev)
		_rtw_memcpy(ndev->dev_addr, (void *)mac, ETH_ALEN);

	rtw_ps_deny(g_padapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(g_padapter);
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_hal_change_macaddr_mbid(g_padapter, (u8 *)mac);
#else
	rtw_hal_set_hwreg(g_padapter, HW_VAR_MAC_ADDR, (u8 *)mac);
#endif
	rtw_ps_deny_cancel(g_padapter, PS_DENY_IOCTL);

	rtl_printk("RTL8814Native: radio MAC set to %02X:%02X:%02X:%02X:%02X:%02X\n",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 0;
}

static void rtl8814_fill_rate_diag(struct rtl8814_rate_diag *r)
{
	struct mlme_ext_priv *pmlmeext = &g_padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &g_padapter->mlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(g_padapter);
	struct sta_info *psta;
	u32 i;

	r->last_rx_rate = g_rate_diag.last_rx_rate;
	r->last_rx_bw = g_rate_diag.last_rx_bw;
	r->last_rx_sgi = g_rate_diag.last_rx_sgi;
	r->last_rx_ldpc = g_rate_diag.last_rx_ldpc;
	r->last_rx_stbc = g_rate_diag.last_rx_stbc;
	r->rx_xfers = g_rate_diag.rx_xfers;
	r->rx_xfer_bytes = g_rate_diag.rx_xfer_bytes;
	r->rx_xfer_max_len = g_rate_diag.rx_xfer_max_len;
	r->rx_xfer_max_pkts = g_rate_diag.rx_xfer_max_pkts;
	r->rx_xfer_pkts = g_rate_diag.rx_xfer_pkts;
	r->rx_skb_recycle = g_rx_skb_recycle;
	r->rx_skb_allocs = g_rate_diag.rx_skb_allocs;
	r->rx_cb_cnt = g_rate_diag.rx_cb_cnt;
	r->rx_cb_ns = g_rate_diag.rx_cb_ns;
	r->rx_cb_ns_max = g_rate_diag.rx_cb_ns_max;
	r->rx_parse_ns = g_rate_diag.rx_parse_ns;
	r->rx_parse_ns_max = g_rate_diag.rx_parse_ns_max;
	r->rx_backlog_max = g_rate_diag.rx_backlog_max;
	r->ampdu_drop = (uint32_t)g_padapter->recvpriv.dbg_rx_ampdu_drop_count;
	r->ampdu_forced_indicate = (uint32_t)g_padapter->recvpriv.dbg_rx_ampdu_forced_indicate_count;
	r->ampdu_loss = (uint32_t)g_padapter->recvpriv.dbg_rx_ampdu_loss_count;
	r->ampdu_window_shift = (uint32_t)g_padapter->recvpriv.dbg_rx_ampdu_window_shift_cnt;
	r->rx_drop = (uint32_t)g_padapter->recvpriv.dbg_rx_drop_count;
	r->warm_start = g_rate_diag.warm_start;

	r->cur_bw = pmlmeext->cur_bwmode;
	r->cur_ch_offset = pmlmeext->cur_ch_offset;
	r->wireless_mode = pmlmeext->cur_wireless_mode;
	r->hal_tx_nss = GET_HAL_TX_NSS(g_padapter);
	r->hal_rx_nss = GET_HAL_RX_NSS(g_padapter);
	if (dvobj) {
		r->cur_rx_tp = dvobj->traffic_stat.cur_rx_tp;
		r->cur_tx_tp = dvobj->traffic_stat.cur_tx_tp;
	}
	if (!check_fwstate(pmlmepriv, _FW_LINKED))
		return;

	r->ht_option = pmlmepriv->htpriv.ht_option;
	r->drv_tx_bw_mode = g_padapter->driver_tx_bw_mode;
	r->drv_rx_ampdu_factor = g_padapter->driver_rx_ampdu_factor;
	r->fix_rx_ampdu_accept = g_padapter->fix_rx_ampdu_accept;
	r->num_tx_ok_period = pmlmepriv->LinkDetectInfo.NumTxOkInPeriod;
#ifdef CONFIG_80211AC_VHT
	r->vht_option = pmlmepriv->vhtpriv.vht_option;
	r->vht_sgi_80m = pmlmepriv->vhtpriv.sgi_80m;
	r->vht_highest_rate = pmlmepriv->vhtpriv.vht_highest_rate;
	r->vht_ampdu_len = pmlmepriv->vhtpriv.ampdu_len;
#endif
	psta = rtw_get_stainfo(&g_padapter->stapriv, pmlmepriv->cur_network.network.MacAddress);
	if (!psta)
		return;
	r->sta_bw = psta->cmn.bw_mode;
	r->ampdu_tx_tids = psta->htpriv.agg_enable_bitmap;
	r->sta_ampdu_enable = psta->htpriv.ampdu_enable;
	r->tx_candidate_tids = psta->htpriv.candidate_tid_bitmap;
	r->ra_bw = psta->cmn.ra_info.ra_bw_mode;
	r->tx_rate = rtw_get_current_tx_rate(g_padapter, psta);
	r->tx_sgi = rtw_get_current_tx_sgi(g_padapter, psta);
	r->tx_bw = psta->cmn.ra_info.curr_tx_bw;
	r->rx_rate = psta->curr_rx_rate;
	r->ra_rate_id = psta->cmn.ra_info.rate_id;
	r->sta_mimo_type = (u8)psta->cmn.mimo_type;
	r->ra_mask = psta->cmn.ra_info.ramask;
	{
		/* The AP's own capabilities, straight from its beacon IEs */
		WLAN_BSSID_EX *bss = &pmlmepriv->cur_network.network;
		sint ielen = 0;
		u8 *ie;
		if (bss->IELength > _FIXED_IE_LENGTH_) {
			ie = rtw_get_ie(bss->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &ielen,
					bss->IELength - _FIXED_IE_LENGTH_);
			if (ie && ielen >= 7) /* cap info 2, A-MPDU params 1, MCS set */
				_rtw_memcpy(r->ap_ht_mcs, ie + 2 + 3, 4);
			ie = rtw_get_ie(bss->IEs + _FIXED_IE_LENGTH_, EID_VHTCapability, &ielen,
					bss->IELength - _FIXED_IE_LENGTH_);
			if (ie && ielen >= 12) { /* cap info 4, RX map 2, RX rate 2, TX map 2 */
				_rtw_memcpy(r->ap_vht_rx_map, ie + 2 + 4, 2);
				_rtw_memcpy(r->ap_vht_tx_map, ie + 2 + 8, 2);
			}
		}
	}
#ifdef CONFIG_80211AC_VHT
	_rtw_memcpy(r->sta_vht_map, psta->vhtpriv.vht_mcs_map, 2);
#endif
	for (i = 0; i < TID_NUM; i++) {
		struct recv_reorder_ctrl *ctl = &psta->recvreorder_ctrl[i];
		if (!ctl->enable)
			continue;
		r->ba_rx_tids |= (u8)(1u << i);
		if (ctl->wsize_b > r->ba_rx_wsize)
			r->ba_rx_wsize = ctl->wsize_b;
	}
}

void rtl8814native_get_link_status(struct rtl8814_link_status *out)
{
	extern volatile uint32_t g_rx_urb_submit_ok;
	extern volatile uint32_t g_rx_urb_submit_err;
	extern volatile uint32_t g_rx_urb_complete_ok;
	extern volatile uint32_t g_rx_urb_complete_err;
	extern volatile uint32_t g_rx_frames_decoded;

	if (!out)
		return;
	_rtw_memset(out, 0, sizeof(*out));
	if (!g_padapter)
		return;

	struct mlme_priv *pmlmepriv = &g_padapter->mlmepriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(g_padapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(g_padapter);

	out->connected = check_fwstate(pmlmepriv, _FW_LINKED) ? 1 : 0;
	out->wpa2_state = (uint32_t)rtl8814_wpa2_get_state();
	out->channel = g_padapter->mlmeextpriv.cur_channel;
	if (pHalData) {
		out->rfe_type = pHalData->rfe_type;
		out->rf_type = pHalData->rf_type;
	}
	if (rfctl) {
		out->channel_plan = rfctl->ChannelPlan;
		out->max_chan_nums = rfctl->max_chan_nums;
	}
	out->rx_urb_submit_ok = g_rx_urb_submit_ok;
	out->rx_urb_submit_err = g_rx_urb_submit_err;
	out->rx_urb_complete_ok = g_rx_urb_complete_ok;
	out->rx_urb_complete_err = g_rx_urb_complete_err;
	out->rx_frames_decoded = g_rx_frames_decoded;
	out->cmd_issued_cnt = g_padapter->cmdpriv.cmd_issued_cnt;
	out->scan_state = mlmeext_scan_state(&g_padapter->mlmeextpriv);
	out->scanned_bss_cnt = g_padapter->mlmeextpriv.sitesurvey_res.bss_cnt;
	_rtw_memcpy(&out->trx, &g_trx_diag, sizeof(out->trx));
	out->usb_speed = g_dvobj ? g_dvobj->usb_speed : 0;
	out->bulk_in_mps = g_bulk_in_mps;
	out->usb_switch = g_usb_switch;
	out->rx_reassemble = g_rx_reassemble;
	if (!RTW_CANNOT_IO(g_padapter)) {
		out->reg_rcr = rtw_read32(g_padapter, REG_RCR);
		out->reg_rxfltmap0 = rtw_read16(g_padapter, REG_RXFLTMAP0);
		out->reg_rxfltmap2 = rtw_read16(g_padapter, REG_RXFLTMAP2);
		out->rate.reg_trxdma_ctrl = rtw_read16(g_padapter, REG_TRXDMA_CTRL_8814A);
		out->rate.reg_rxdma_agg_pg_th = rtw_read32(g_padapter, REG_RXDMA_AGG_PG_TH_8814A);
		out->rate.reg_rxdma_mode = rtw_read32(g_padapter, REG_RXDMA_MODE_8814A);
	}
	rtl8814_fill_rate_diag(&out->rate);

	if (out->connected) {
		WLAN_BSSID_EX *cur = &pmlmepriv->cur_network.network;
		out->rssi = (int32_t)cur->Rssi;
		_rtw_memcpy(out->bssid, cur->MacAddress, 6);
		u32 slen = cur->Ssid.SsidLength;
		if (slen > 32)
			slen = 32;
		if (slen > 0)
			_rtw_memcpy(out->ssid, cur->Ssid.Ssid, slen);
		out->ssid[slen] = '\0';
	}
}

void rtl8814native_get_rx_log(struct rtl8814_rx_log *out)
{
	if (out)
		_rtw_memcpy(out, &g_rx_log, sizeof(*out));
}

int rtl8814native_reg_access(uint32_t op, uint32_t width, uint32_t addr, uint32_t value, uint32_t *out)
{
	if (!g_padapter || !out || RTW_CANNOT_IO(g_padapter))
		return -ENODEV;

	switch (op) {
	case 0:
		if (addr >= 0x4000)
			return -EINVAL;
		if (width == 1)
			*out = rtw_read8(g_padapter, addr);
		else if (width == 2)
			*out = rtw_read16(g_padapter, addr);
		else
			*out = rtw_read32(g_padapter, addr);
		return 0;
	case 1:
		if (addr >= 0x4000)
			return -EINVAL;
		if (width == 1)
			rtw_write8(g_padapter, addr, (u8)value);
		else if (width == 2)
			rtw_write16(g_padapter, addr, (u16)value);
		else
			rtw_write32(g_padapter, addr, value);
		*out = value;
		return 0;
	case 3:
		switch (addr) {
		case RTL_OPT_RX_REASSEMBLE:
			*out = g_rx_reassemble;
			g_rx_reassemble = value ? 1 : 0;
			return 0;
		case RTL_OPT_RX_SKB_RECYCLE:
			*out = g_rx_skb_recycle;
			g_rx_skb_recycle = value ? 1 : 0;
			return 0;
		case RTL_OPT_RESET_PERF:
			*out = 0;
			g_rate_diag.rx_xfers = 0;
			g_rate_diag.rx_xfer_bytes = 0;
			g_rate_diag.rx_xfer_pkts = 0;
			g_rate_diag.rx_xfer_max_len = 0;
			g_rate_diag.rx_xfer_max_pkts = 0;
			g_rate_diag.rx_skb_allocs = 0;
			g_rate_diag.rx_cb_cnt = 0;
			g_rate_diag.rx_cb_ns = 0;
			g_rate_diag.rx_cb_ns_max = 0;
			g_rate_diag.rx_parse_ns = 0;
			g_rate_diag.rx_parse_ns_max = 0;
			g_rate_diag.rx_backlog_max = 0;
			return 0;
		default:
			return -EINVAL;
		}
	case 2: {
		u32 path = (addr >> 8) & 0xFF;
		if (path > RF_PATH_D)
			return -EINVAL;
		*out = rtw_hal_read_rfreg(g_padapter, (enum rf_path)path, addr & 0xFF, 0xFFFFF);
		return 0;
	}
	default:
		return -EINVAL;
	}
}

int rtl8814native_vendor_xmit(struct sk_buff *skb)
{
	if (!skb)
		return -EINVAL;
	if (!g_padapter || !g_padapter->bup || !g_tx_work_inited) {
		dev_kfree_skb_any(skb);
		return -ENODEV;
	}
	skb->dev = g_padapter->pnetdev;
	skb_queue_tail(&g_tx_skb_queue, skb);
	schedule_work(&g_tx_work);
	return 0;
}
