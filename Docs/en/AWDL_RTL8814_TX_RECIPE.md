# AWDL on the RTL8814AU: how to transmit PSF, MIF and data of `awdl0`

Concrete recipe, taken from the driver tree, to emit the AWDL frames over the air: the sync action
frames (PSF/MIF) and the data frames of `awdl0` toward a peer. Complements
[AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4 (management path and descriptor
fields) and [AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) (addresses and ACK). Format of the
frames: [AWDL_REFERENCIAS_EXTERNAS.md](AWDL_REFERENCIAS_EXTERNAS.md) §4 and §5. What is not checked on
hardware goes with `?`.

## 1. The model: the monitor-mode injection of the vendor

`rtw_monitor_xmit_entry` (`Source/rtl8814au/core/rtw_xmit.c:4445`) emits a complete 802.11 frame
built by the caller, be it management or data:

1. `alloc_mgtxmitframe` (body in `_alloc_mgtxmitframe`, `core/rtw_mlme_ext.c:7496`) reserves an
   `xmit_frame` from the management queue.
2. It zeroes the header and copies the frame into `buf_addr + TXDESC_OFFSET`.
3. Attributes according to the type of the frame:
   - data: `update_monitor_frame_attrib` (`core/rtw_mlme_ext.c:7555`), with a rate of 24 Mbit/s if
     A1 or A3 are broadcast;
   - management: `update_mgntframe_attrib` (`:7616`).
4. `retry_ctrl = FALSE`, `pktlen` and `last_txcmdsz` = length, and sequence number taken from the
   header.
5. `dump_mgntframe` (`:7734`) → `rtw_hal_mgnt_xmit` → descriptor and USB pipe.

The function is Linux code: it receives an `sk_buff` and is under an `LINUX_VERSION_CODE` guard. It serves
as a model; the layer needs its own entry doing the same steps from an `mbuf` or from an
own buffer.

## 2. What is not touched along the way

| Point | Behavior | Citation |
|---|---|---|
| Addresses | `update_mgntframe_attrib_addr` only copies A1/A2 from the frame into `ra`/`ta`; it does not rewrite them. A2 can be the MAC of `awdl0` and A3 the BSSID `00:25:00:FF:94:73` | `core/rtw_mlme_ext.c:7691` |
| Broadcast | The `BMC` bit of the descriptor comes from `IS_MCAST(pattrib->ra)`: a PSF/MIF to `FF:FF:FF:FF:FF:FF` is marked by itself | `hal/rtl8814a/usb/rtl8814au_xmit.c:54`, `:88-89` |
| Sequence number | Without QoS the hardware sets it (`HWSEQ_EN`) | `rtl8814au_xmit.c:111-117` |
| Port PMF | `rtl8814_wpa2_pmf_protect_tx` only encrypts management with A1 = BSSID of the AP and robust category; the AWDL AFs (broadcast, category 127), the ADDBA responses to a peer (A1 ≠ AP) and the data frames go out intact | `Source/Driver/WPA2Supplicant.c:607` |
| Encryption | `encrypt = _NO_PRIVACY_` in both helpers | `core/rtw_mlme_ext.c:7555`, `:7616` |

## 3. What has to be set by hand

### 3.1 Rate

- **Management** (PSF/MIF, ADDBA): `pmlmeext->tx_rate`, which is 6 Mbit/s OFDM at 5 GHz and 1 Mbit/s CCK
  at 2.4 GHz ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4.1). It is changed with
  `update_mgnt_tx_rate` (`core/rtw_mlme_ext.c:7546`). On channel 6, 6 Mbit/s OFDM is advisable: a PSF of
  about 200 B at 1 Mbit/s occupies more than 1.6 ms, and the peer only passes one slot of every 16 on that channel
  (ESP32Drop measured that CCK at 1 Mbit/s fills almost the whole of that slot).
- **Data**: `update_monitor_frame_attrib` sets `MGN_VHT1SS_MCS9` with `bwmode = 20 MHz`. VHT MCS 9 at
  20 MHz is not valid in 802.11ac with 1 stream, so `pattrib->rate` must be overwritten after the
  call. While the layer does not announce HT/VHT ([AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) §6),
  the coherent choice is a legacy rate (24 Mbit/s, the one the path itself uses for broadcast) or HT MCS 7 at
  20 MHz `?`.

### 3.2 Retries

The descriptor has a 6-bit retry limit (`DATA_RETRY_LIMIT`,
`include/rtl8814a_xmit.h:189`). The management branch always activates it: 6 with `retry_ctrl = TRUE` and
**12** with `FALSE` (`rtl8814au_xmit.c:259-263`). That `FALSE` does not mean "without retries" matters
because a unicast retried beyond the visit of the peer blocks the queue (owly, `tx_noack`).
Options:

- data unicast: `retry_ctrl = TRUE` (6), close to the 7 of winawdl;
- if a single attempt is wanted, write its own limit into the descriptor; the value equivalent to
  "no retry" on this MAC is not checked `?`;
- broadcast (PSF/MIF): the MAC does not wait for an ACK of a group frame, so the limit is irrelevant
  `?`.

### 3.3 ACK back

For a unicast to a peer to be deemed delivered, the MAC has to recognize the ACK addressed to A2
(the MAC of `awdl0`). It is the same requirement as the ACK on reception: port 1 with
`REG_MACID1` = MAC of `awdl0` ([AWDL_RTL8814_RX_ACK.md](AWDL_RTL8814_RX_ACK.md) §4). Without it, each
unicast would exhaust its retries even though the peer receives it `?`.

## 4. The frames

**PSF / MIF** (management, action subtype):

```
FC 0xD0 0x00 | dur 0 | A1 FF:FF:FF:FF:FF:FF | A2 MAC awdl0 | A3 00:25:00:FF:94:73 | seq (HW)
7F | 00 17 F2 | 08 | version | subtype (0 PSF, 3 MIF) | 00 | phy_tx (u32) | target_tx (u32) | TLVs
```

The TLVs come from the template of ioctl 151 plus TLVs 4 and 18 the layer composes
([AWDL_CHANNEL_SEQUENCE_CONTROL.md](AWDL_CHANNEL_SEQUENCE_CONTROL.md) §5.2; emission detail in
`AWDL_PSF_MIF_EMISSION.md`, in progress). `phy_tx` and `target_tx` are µs of the layer's clock: the predicted
instant of the slot and the one of delivery to the queue. The receiver's kernel computes with them
`sync_contention` ([AWDL_FRAME_WIRE_FORMAT.md](AWDL_FRAME_WIRE_FORMAT.md) §5).

**Data of `awdl0`** (from the TX queue of Skywalk, stage 2b):

```
FC 0x08 0x00 (or 0x88 with QoS) | A1 MAC of the peer | A2 MAC awdl0 | A3 00:25:00:FF:94:73 | seq
AA AA 03 | 00 17 F2 | 08 00 | 03 04 | AWDL seq (u16 LE) | 00 00 | ethertype | IPv6
```

The packet reaches the driver as pure Ethernet II and the AWDL header is put by the driver, with its own
sequence counter per destination ([AWDL_DATA_ENCAP.md](AWDL_DATA_ENCAP.md) §1 and §7).
`IO80211AWDLPeerManager::addAwdlSnapHeader` (`0xffffff8002216cd0`) has no references on 25G241: the
family does not use it.

## 5. Emission moment

The descriptor carries no emission instant. The layer enqueues within the window marked by its own
timer ([AWDL_WINDOW_TIMER.md](AWDL_WINDOW_TIMER.md) §5 and §6) and can use `REG_TXPAUSE` as
queue gate ([AWDL_RTL8814_HW_SUPPORT.md](AWDL_RTL8814_HW_SUPPORT.md) §4.4). The latency between
the enqueuing and the exit over USB is not measured `?`.
