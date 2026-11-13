# The channel switch offload of the RTL8814A firmware (H2C 0x1C and 0x87)

Closes the line open in `AWDL_FW_8814A_MCC.md` §8 (last point: the switch offload
0x1C/0x87 "exists in the firmware, although the tree driver does not use it and its real latency is `?`")
and the discard of `AWDL_MCC_TIEMPO_COMPARTIDO.md` §3.3 (the offload was abandoned because it requires
`RegFWOffload == 2`). It documents what exactly the two channel switch handlers of
firmware v33.6 (2019-06-11) do: the H2C `CHNL_SWITCH_OPER_OFFLOAD` 0x1C (`sub_8000812C`) and the
H2C `CHNL_SWITCH_OFFLOAD` 0x87, WoWLAN variant (`sub_8000817C`), with the argument layout,
all the register and RF writes they execute, the C2H 0x10 they return and the wait loops that
bound the latency; and it compares them step by step with what the tree driver
(`PHY_HandleSwChnlAndSetBW8814A`) does over USB.

Addresses in hex without prefix = firmware addresses (IRAM at 0x80000000, DMEM at
0x80200000, same convention as `AWDL_FW_8814A_MCC.md`). Unverified items are marked `?`. Nothing of
this has been measured on hardware: the latency remains pending of measurement, not of estimation.

**Short answer.** Both handlers do in the MCU, at register speed, exactly the physical part
of a channel switch of the 8814A: table `fc_area` (BB 0x860), write of `RF_CHNLBW`
(RF register 0x18, retune of the synthesizer) on the 4 paths, per-band AGC table selection
(BB 0x958), TRXPTCL (0x668), `DATA_SC` (0x483), `rRFMOD` (0x8AC), 0x8C4, 0x864, 0x838, 0x848 and the
RF 0x18 width bits — the same registers, masks and values that `phy_SwChnl8814A` +
`phy_SetBwMode8814A` write over USB. They do not reload TX power, do not run IQK, do not touch the
NBI/CSI tables nor the ADC clock, and do not switch band. They return `C2H_FW_CHNL_SWITCH_COMPLETE` (0x10) with the
new channel in the first payload byte and declared length 7. The 0x1C applies the width and the
SC offsets it receives in byte 1; the 0x87 always switches to 20 MHz and verifies by read the
RF change.

## 1. Entry point

The H2C dispatcher `sub_80005668` (full map in `AWDL_FW_8814A_MCC.md` §5) takes from the
DMEM queue (8-byte entries `[id][7 args]`, drained by `sub_80005834` from the main loop
`sub_800016EC`) and calls each handler with the payload pointer in `a0`:

- id 0x1C → `jal sub_8000812C` at 0x8000574E (with `move a0, a1` at 0x8000574C: `a0` points to the
  first payload byte, not to the id);
- id 0x87 → `jal sub_8000817C` at 0x8000580E.

The 0x1D `SINGLE_CHANNELSWITCH_V2` is still an empty stub (`jr ra` at 0x80008128).

## 2. Argument layout of H2C 0x1C

`sub_8000812C` reads three bytes of the payload (`lbu` at 0x80008134-0x8000813A) and slices them thus:

| Byte | Bits | Field | Consumer |
|---|---|---|---|
| 0 | 7:0 | channel (the value that goes into RF 0x18[7:0]) | `sub_800070FC` |
| 1 | 1:0 | width (`CHANNEL_WIDTH_20/40/80` = 0/1/2) | both |
| 1 | 4:2 | 40 MHz SC (offset of the primary within the 40) | `sub_800070B4` |
| 1 | 7:5 | 80 MHz SC (offset of the primary within the 80) | `sub_800070B4` |
| 2 | 3:0 | RFE type | read (0x80008144) and passed to `sub_800070FC`, which ignores it `?` |

Byte 3 and the rest of the queue entry (7 payload bytes available) are not read.

This layout matches byte for byte the macros of the tree sender
(`include/hal_com_h2c.h:458-462`): `SET_H2CCMD_CH_SW_OPER_OFFLOAD_CH_NUM` full byte 0,
`BW_MODE` byte 1 [1:0], `BW_40M_SC` byte 1 [4:2], `BW_80M_SC` byte 1 [7:5],
`RFE_TYPE` byte 2 [3:0]. The driver sends 4 bytes (`ch_sw_h2c_buf[4]`, `hal_com.c:11991-12007`).

## 3. The 0x1C handler step by step (`sub_8000812C`)

1. **Channel part** — 0x8000814A: `sub_800070FC(a0=channel, a1=width, a2=SC40, a3=RFE)`.
   Inside `sub_800070FC` only `a0` is used (`s0 = a0 & 0xFF` at 0x80007106-0x80007108): width,
   SC and RFE are read but do not affect this half. See §4.
2. **Width part** — 0x8000815C: `sub_800070B4(width, SC80, SC40)` with
   `a0 = byte1 & 3` (width), `a1 = byte1 >> 5` (SC 80), `a2 = (byte1 >> 2) & 7` (SC 40)
   (0x8000814E-0x8000815A). See §5.
3. **C2H** — 0x80008160-0x8000816E: composes the record and calls `sub_80004CC8`. See §7.

There is no other path: no prior checks, no early-return, no state.

## 4. `sub_800070FC`: the channel half

Sequence verified in the disassembly (296 bytes):

1. **`fc_area`** (0x80007104-0x80007188): depending on the channel, writes BB 0x860
   (`rFc_area_Jaguar`, mask read from DMEM 0x7E4 = 0x1FFE0000) with
   `sub_80006AE4(0x860, 0x1FFE0000, value)`:

   | Channel | Value 0x860 | Same as driver |
   |---|---|---|
   | 36–48 | 0x494 | `rtl8814a_phycfg.c:1927` |
   | 50–64 | 0x453 | :1929 |
   | 100–116 | 0x452 | :1931 |
   | ≥ 118 | 0x412 | :1933 |
   | rest (2.4 GHz) | 0x96A | :1935 |

2. **Synthesizer retune, the 4 paths** (loop 0x8000718A-0x800071DE, `s1` from 0 to 3): computes the
   `RF_MOD_AG` field with the same table as the driver
   (`36–64 → 0x101`, `100–140 → 0x301`, `>140 → 0x501`, rest `0x000`; driver :1939-1946), assembles
   `value = channel | (RF_MOD_AG << 8)` and calls `sub_80006FE0(path, 0x18, mask 0x703FF, value)`.
   The mask comes from DMEM 0x7EC = 0x000703FF = `BIT18|BIT17|BIT16|BIT9|BIT8|bMaskByte0`, and the
   shift 8 of `sub_800043B8` over DMEM 0x7E8 = 0x00070300 — identical to the
   `BitMask`/`BitShift` of `phy_SwChnl8814A` (:1949-1955). **It does retune the synthesizer**: the low
   byte of RF 0x18 (`RF_CHNLBW`) receives the channel as-is.

3. **Per-band AGC table** (0x800071E0-0x80007216): BB 0x958 (`rAGC_table_Jaguar2`, mask 0x1F)
   = 1 for 36–64, 2 for 100–144, 3 for ≥ 149 — the same cases of the driver (:1958-1963).

Detail of the shared RF/BB machinery (all handler accesses go through here):

- **Masked BB write** `sub_80006AE4(reg, mask, value)`: with a full mask writes 32 bits at
  `0xB8640000 + (reg & 0xFFFF)` (0x80006AFA-0x80006B06); with a partial mask reads the
  register with `sub_80006AAC`, merges in registers and writes (0x80006B08-0x80006B34). The base
  0xB8640000 lives at DMEM 0x4C0 (the BB window; `AWDL_FW_8814A_MCC.md` §3).
- **RF write** `sub_80006FE0(path, reg, mask, value)`: if the mask is not full, reads the
  old value with `sub_800068B0`, merges (`sub_800043B8` gives the lowest-bit shift) and
  calls `sub_80006FA4` (0x80007004-0x80007028); afterwards it **spins a busy delay**
  `sub_800046A8(1)` (0x8000702C-0x8000702E) after each RF write.
- **RF read** `sub_800068B0(path, reg)`: reads 32 bits at
  `{0xB8642800, 0xB8642C00, 0xB8643800, 0xB8643C00}[path] + reg*4`
  (windows at DMEM 0x778-0x784, initialized in the image). These are the same BB addresses that
  `phy_RFRead_8814A` uses over USB (`0x2800/0x2c00/0x3800/0x3c00 + RegAddr*4`,
  `rtl8814a_phycfg.c:101-113`).
- **Three-wire RF write** `sub_80006FA4(path, reg, value)`: writes
  `(reg << 20) | (value & 0xFFFFF)` masked with 0x0FFFFFFF into the BB register
  `*(u16*)(0x80203024 + path*4) + 0xB8640000`. The DMEM 0x3024 table is zero in the image
  and `sub_800068FC` fills it (called from init at 0x80001CAC) with {0xC90, 0xE90, 0x1890,
  0x1A90} = `rA_LSSIWrite_Jaguar`, `rB_LSSIWrite_Jaguar`, `rC_LSSIWrite_Jaguar2`,
  `rD_LSSIWrite_Jaguar2` — the same `rf3wireOffset` the driver assembles
  (`rtl8814a_phycfg.c:259-274`; defines in `Hal8814PhyReg.h:72-82`). The word format is the
  one of `phy_RFWrite_8814A` (:152-155).
- **Delay** `sub_800046A8(n)`: busy loop of `n × *(DMEM 0x802016A0)` iterations
  (0x800046A8-0x800046C2). The unit is set at startup by `sub_800045A4` (0x800045A4-0x8000467A)
  according to a requested value (10/20/25/40/50/80/100/200 → units 2/3/9/5/17/10/34/67): it is the
  firmware "µs wait" calibration, so one unit is on the order of the µs `?`. With `n=1` after each
  RF write the added cost is small but not statically quantifiable `?`.

Count of the channel half: 1 BB write (0x860) + 4×(1 read + 1 write RF) +
1 BB write (0x958) ≈ 10 register operations, all inside the MCU.

## 5. `sub_800070B4`: the width half

With `(a0=width, a1=SC80, a2=SC40)` (0x800070BC-0x800070CA):

1. **TRXPTCL** — `sub_8000693C(width)` (0x800070CA): reads 16 bits of 0xB8640668
   (`REG_TRXPTCL_CTL_8814A` = 0x668, pointers at DMEM 0x78C/0x790), clears bits 0/7/8 and
   sets: width 0 → none (0x8000695C: mask 0xFE7F), width 1 → bit 7 (0x80006964), width 2 →
   bit 8 (0x80006970); writes the low byte at 0x668 and the high at 0x669. It is `phy_SetBwRegMac_8814A`
   (:1282-1310) with the same masks (0xFE7F / bit 7 with 0xFEFF / bit 8 with 0xFF7F); only
   divergence: the firmware additionally forces bit 0 to 0 at widths 1 and 2 (mask ~0x181), the
   driver preserves it `?` (bit 0 of 0x668 unnamed in the tree).
2. **`DATA_SC`** — `sub_80006998(width, SC80, SC40)` (0x800070D4) computes the SC byte and
   `sub_800070B4` itself writes it as one byte at 0xB8640483 = `REG_DATA_SC_8814A` (0x483; pointer at
   DMEM 0x7E0) (0x800070D8-0x800070E4). The table of `sub_80006998` is exactly the one of
   `phy_GetSecondaryChnl_8814A` (:1233-1280): with width 2 (80 MHz), high nibble = 0xA if SC80 =
   LOWER (1), 9 if UPPER (2); low nibble = 4 (20_LOWEST) with {SC40=1,SC80=1}, 1 (20_UPPER) with
   {SC40=2,SC80=1}, 2 (20_LOWER) with {SC40=1,SC80=2}, 3 (20_UPPERST) with {SC40=2,SC80=2}; with
   width 1 (40 MHz) = 1 if SC40 = 2, 2 if SC40 = 1; with width 0 it returns 0. The enums match
   (`VHT_DATA_SC_*`, `rtw_rf.h:129-141`; `HAL_PRIME_CHNL_OFFSET_*`, `rtw_rf.h:87-89`). The firmware
   adds a 20 MHz mapping the driver does not have (it uses SC80/SC40 even though the width is 20); it is
   unreachable with the tree sender, which leaves both SC at 0 at 20 MHz.
3. **Width BB configuration** — `sub_80006E08(width, SC40)` (0x800070E6; inside, `s0` = SC40):

   | Register | Mask (DMEM) | Value per width | Note |
   |---|---|---|---|
   | 0x8AC `rRFMOD` | 0x003003C3 (0x7B8) | 0x00300200 (20) / 0x00300201 (40) / 0x00300202 (80) | sets [1:0] = width, [9:8] = 2, [21:20] = 3, [7:6] = 0. The driver only writes [1:0] (`phy_SetBwRegAdc_8814A`, :918-947); the other fields remain in the driver as the init left them `?` |
   | 0x8C4 | 0x40000000 (0x3DC) | 0 (20/40), 0x40000000 (80) | register unnamed in the tree |
   | 0x8AC [5:2] | 0x3C | SC40 << 2 (40 and 80) | same as `phy_SetBwMode8814A` :1688/:1697 |
   | 0x838 `rCCAonSec_Jaguar` | 0xF0000000 (0x578) | SC40 << 28 (40 and 80) | the driver does not touch it in its width path |
   | 0x864 | 0x001C0000 (0x7C0) | 0x00100000 (20), 0x00080000 (40/80) | `rFPGA0_XB_RFInterfaceOE` reused; the driver does not touch it here |
   | 0x848 `rL1PeakTH_Jaguar` | 0x03C00000 (0x7C8) | 0x01800000 (40), 0x01400000 (80) | conditioned on the read of 0x834 (`rBWIndication_Jaguar`) having bit 26 (DMEM mask 0x4F0 = 0x04000000) (0x80006ED4-0x80006EF6 and 0x80006F74-0x80006F92) `?` |
   | 0xA00 `rCCK_System_Jaguar` | 0x10 | bit 4 = 1 if SC40 = 1, 0 otherwise (40 MHz only) | same as :1690-1693 (`VHT_DATA_SC_20_UPPER_OF_80MHZ` = 1) |

   It does not touch 0x82C (`rAGC_table_Jaguar`, the per-width AGC selection the driver does write in
   `phy_SetBwRegAgc_8814A` :953-981) nor 0x8C8.
4. **RF width bits** — `sub_8000703C(width)` (0x800070EC): writes RF 0x18 mask 0xC00 on
   paths 0 and 1: 20 MHz → 0xC00, 40 MHz → 0x400, 80 MHz → 0x000. Same values as
   `PHY_RF6052SetBandwidth8814A` (`rtl8814a_rf6052.c:43-62`), but **only paths A and B**: C and D
   keep their previous width bits `?`.

Count of the width half: 2 (TRXPTCL, DATA_SC) + ~6 BB + 2×(1R+1W) RF ≈ 12 operations, plus
1 BB read (0x834).

## 6. The 0x87 handler (`sub_8000817C`), WoWLAN variant

1. 0x8000818C: `sub_800070FC(channel, 0, 0, byte2)` — channel from the argument, **width 0 and SC 0
   forced** (always 20 MHz; consistent with the WoWLAN use of waking on the base channel). Byte
   2 is read but `sub_800070FC` ignores it.
2. 0x80008198: `sub_80006A68(0, 0x18, -1)` — full read of RF 0x18 of path 0 (via
   `sub_800068B0`; `sub_80006A68` at 0x80006A68-0x80006AAC with mask −1 = direct read).
3. 0x8000819C-0x800081AC: if `(read & 0xFF) != channel`, repeats `sub_800070FC(channel, 0, 0, byte2)`
   once. No more retries nor loop: at most two attempts, with no intermediate wait beyond the
   internal delays of each RF write (§4).
4. C2H (§7).

The driver variant that would invoke it is in `phy_SwChnl8814A` (:1922-1923): a single byte
(channel), and only with `RegFWOffload == 2`. For AWDL it adds nothing over the 0x1C: it fixes 20 MHz and does
not accept SC.

## 7. The C2H 0x10 they return

Both handlers compose the 16-byte record on the stack and call `sub_80004CC8`, which copies it
to the event ring (ring, indices and overflow bit described in `AWDL_FW_8814A_MCC.md` §6):

| Offset | Value | Write |
|---|---|---|
| +0 | 0x10 (`C2H_FW_CHNL_SWITCH_COMPLETE`) | 0x80008166 / 0x800081B4 |
| +1 | not written (prior stack byte) `?` | — |
| +2 | new channel (byte 0 of the H2C) | 0x8000816C / 0x800081BE |
| +3..+13 | not written `?` | — |
| +14 | 7 (low byte of the halfword written with `sh`) | 0x80008164 / 0x800081B8 |

Byte +14 is the length field of the C2H format the host reads: `C2H_PLEN_88XX` is at
byte 14 (`hal/hal_com_c2h.h:43`; id at +0, seq at +1, payload from +2, :40-42). So the C2H 0x10
declares **7 payload bytes**, of which only the first has guaranteed content (the channel);
the other six are stack residue `?`. The halfword +14/+15 (7) of the 0x1C/0x87 senders matches
the pattern of other senders (C2H_TXBF 0x02 and C2H_BCN_EARLY_RPT 0x1E write 1 with a single payload
byte: `sub_800075E4` at 0x80007680-0x80007688, `sub_80002884` at 0x8000289E-0x800028A8).

Consumption by the driver: over USB the C2H arrives as an RX packet (`pkt_rpt_type == C2H_PACKET` →
`rtw_hal_c2h_pkt_pre_hdl`, `hal/rtl8814a/usb/usb_ops_linux.c:181-183`) and
`rtw_hal_c2h_pkt_hdr_parse` splits id = buf[0], seq = buf[1], payload = buf+2 and plen = packet length − 2
(`hal_intf.c:1177-1190`); the register-based read uses the plen of byte 14
(`c2h_evt_read_88xx`, `hal_com.c:1390-1400`). `c2h_handler_8814a` only serves
0x02/0x03/0x04/0x0D and returns `_FAIL` for the rest (`rtl8814a_cmd.c:725-749`), so the 0x10
falls to the generic handler: the `C2H_FW_CHNL_SWITCH_COMPLETE` case (→ `rtw_tdls_chsw_oper_done`) and its
inclusion in "handle directly" (`hal_intf.c:1231-1235` and :1311-1313) only compile with
`CONFIG_TDLS && CONFIG_TDLS_CH_SW`. Without TDLS_CH_SW the C2H 0x10 is discarded with no effect.

## 8. What the offload does NOT do

- **Band switching**: neither the 0x1C nor the 0x87 touches `REG_CCK_CHECK_8814A` (0x454) nor runs
  the equivalent of `phy_SwBand8814A`/`PHY_SwitchWirelessBand8814A` (`rtl8814a_phycfg.c:988-1034`).
  A 2.4 ↔ 5 GHz hop through this path would be incomplete `?`. Irrelevant for 149 ↔ 153.
- **TX power reload**: no write to 0x1998 nor to the `rTxAGC_*` tables (the 0x834/0x838/
  0x848 the width writes are other fields). It is the piece that in the driver costs ~252
  USB writes (`AWDL_MCC_TIEMPO_COMPARTIDO.md` §4).
- **IQK**: no access to the IQK registers nor to the calibration block (the DM class 0x44/0x45
  exists in the dispatcher but is not part of this path).
- **Spur/NBI/CSI and ADC clock**: nothing equivalent to `phy_SpurCalibration_8814A` (:1464, only acts
  with `rfe_type == 0` on concrete channels and elsewhere resets NBI/CSI) nor to
  `phy_ADC_CLK_8814A` (:446, cut A only).
- **Driver state**: the firmware does not touch `dvobj->oper_*`, `pHalData->current_*`,
  `mlmeextpriv.cur_*` nor the phydm pointers (`ODM_CMNINFO_BW/CHNL`, hooked to
  `pHalData->current_channel_bw/current_channel` in `hal/hal_dm.c:443-444`). Whoever uses the offload
  must maintain those states by hand.
- **802.11 announcements**: without null data PM nor media-status: that remains the driver's.

## 9. Step-by-step comparison with `PHY_HandleSwChnlAndSetBW8814A` over USB

Starting point of the driver: `set_channel_bwmode` (`core/rtw_wlan_util.c:596`) passes the **center
channel** to `rtw_hal_set_chnl_bw` (`hal_intf.c:1001`; "set center channel", `rtw_wlan_util.c:657`)
→ `PHY_SetSwChnlBWMode8814` (`rtl8814a_phycfg.c:2342`) → `PHY_HandleSwChnlAndSetBW8814A` (:2222,
change detection :2249-2273 and early-return at :2277) → `phy_SwChnlAndSetBwMode8814A` (:2078) →
`phy_SwChnl8814A` (:1883) and `phy_SetBwMode8814A` (:1660), and at the end TX power (:2117-2119,
conditioned on `awdl_keep_txpwr` in this tree) and IQK only with `bNeedIQK` (:2121).

For the AWDL case of interest (associated on 153/80, slot on 149/80 and back):

| Step | Firmware H2C 0x1C | Driver over USB | Equivalence |
|---|---|---|---|
| Prior check | none | early-return if channel/width/SC/center do not change (:2249-2273) | the offload rewrites everything; with the same values it is harmless |
| Band | no | `phy_SwBand8814A` (:1897) — no effect 5G↔5G | same effect |
| `fc_area` 0x860 | yes, same table | :1926-1935 | identical |
| RF 0x18, 4 paths, mask 0x703FF | yes (§4, point 2) | :1937-1956 | identical; byte 0 = received channel (see §10) |
| Band AGC 0x958 | yes, same table | :1958-1963 | identical |
| TRXPTCL 0x668 | yes (§5.1) | `phy_SetBwRegMac_8814A` :1666 | identical except bit 0 |
| `DATA_SC` 0x483 | yes, same SC table | :1670 + `phy_GetSecondaryChnl_8814A` :1233 | identical for 40/80 |
| `rRFMOD` 0x8AC | yes, more fields | :918-947 and :1688/:1697 | the firmware sets [21:20]/[9:8]/[7:6] that the driver does not touch here `?` |
| 0x8C4, 0x864, 0x838, 0x848 | yes (§5.3) | not in this path | firmware extra `?` |
| 0xA00 (CCK system) | 40 MHz only | 40 MHz only (:1690-1693) | identical |
| 0x82C (per-width AGC) | no | `phy_SetBwRegAgc_8814A` :981 | divergence: the driver does write it |
| RF 0x18[11:10] width bits | paths A and B (§5.4) | the 4 paths (`rtl8814a_rf6052.c:43-62`) | C/D divergence `?` |
| CCK filters 2.4 GHz | no | :1974-1992 (2.4 GHz only) | irrelevant at 5 GHz |
| Spur/NBI + ADC clock | no | end of `phy_SetBwMode8814A` :1715-1716 | the driver resets NBI/CSI; the firmware does not |
| TX power | no | :2117-2119 (~252 writes) or nothing with `awdl_keep_txpwr` | same effect as the local guard |
| IQK | no | only with `bNeedIQK` | normally does not run |
| Completion notice | C2H 0x10 (§7) | none (synchronous) | the offload is asynchronous |

Host cost: the offload reduces to an H2C of the mailbox (two 32-bit writes, mailbox and
extension, without `CONFIG_H2C_EF`, plus the polling of `REG_HMETFR_8814A` 0x1CC in
`_is_fw_read_cmd_down`, `rtl8814a_cmd.c:28-35` and :99) — 3-5 USB operations — versus the
~280-300 of the full path or ~22 of the same-block variant of the driver
(`AWDL_MCC_TIEMPO_COMPARTIDO.md` §4 and §4.1). Inside the MCU, the full switch is ~20
register operations plus the six RF write delays (§4); the total latency depends on
those delays and on the C2H transport: without measuring, no figure.

## 10. Using the 0x1C to move primary 153 ↔ 149 within the same 80 MHz block

What the tree already has:

- `H2C_CHNL_SWITCH_OPER_OFFLOAD = 0x1C` and `H2C_CHNL_SWITCH_OFFLOAD = 0x87` defined
  (`include/hal_com_h2c.h:46` and :109).
- Sender of the 0x1C: `rtw_hal_ch_sw_oper_offload(padapter, channel, channel_offset, bwmode)`
  (`hal/hal_com.c:11986-12008`), which assembles the buffer with the macros of §2 and calls
  `rtw_hal_fill_h2c_cmd`. It is entirely under `CONFIG_TDLS_CH_SW`, which is not defined
  (`include/autoconf.h:168`, commented out), inside `CONFIG_TDLS`, which is also not defined
  (block `autoconf.h:161-169` without `#define CONFIG_TDLS` in the tree). Its only caller is the
  TDLS one (`core/rtw_tdls.c:1006`). Result: **the 0x1C is never sent as the tree stands**.
- Transport: `FillH2CCmd_8814` (`rtl8814a_cmd.c:60`) through mailboxes 0x1D0-0x1DF/0x1F0-0x1FF, which
  is the mechanism the firmware consumes via the DMEM queue (`AWDL_FW_8814A_MCC.md` §4).
- Already-written receiver of the C2H 0x10 (dormant behind the same ifdefs): §7.
- The 0x87 is wired in `phy_SwChnl8814A` (:1922-1923) after `RegFWOffload == 2`, but
  `RegFWOffload` is not assigned anywhere in the tree (only the declaration in
  `include/hal_data.h:357`; the other two uses are dead: `#if 0` at :668-672 and the one of
  `hal/phydm/rtl8814a/phydm_rtl8814a.c:192`, inside an `ODM_WIN` block starting at :124).
  It stays at 0 and that branch does not fire.

What would be needed for the 153/80 ↔ 149/80 hop via 0x1C:

1. Making the sender available outside TDLS (removing the ifdefs of `rtw_hal_ch_sw_oper_offload` or adding
   a custom caller) and deciding whether to wait for the C2H 0x10 (enabling the generic case) or go
   fire-and-forget.
2. Passing the **center channel (155) in byte 0**, not the primary: the firmware writes that value
   as-is into RF 0x18[7:0] and into the AGC table selection, and the driver's software path works with
   `current_channel = center` (`rtw_hal_set_chnl_bw` receives `center_ch`,
   `rtw_wlan_util.c:657`). The TDLS caller passes the primary (`rtw_tdls.c:1006`) — at 20/40
   center and primary differ just the same; with the image it cannot be known whether the Realtek TDLS
   counted on another convention `?`.
3. Passing in byte 1: width 2 (80 MHz), `BW_80M_SC` = LOWER (1, fixed for the 149-161 block with
   center 155) and `BW_40M_SC` = 2 when going to 153, 1 when going to 149. With that the firmware writes
   `DATA_SC` 0xA1 ↔ 0xA4 and 0x838[31:28] 0x2 ↔ 0x1 (the only changing values), and rewrites
   identical 0x860, RF 0x18 (155), 0x958 (3), 0x668, 0x8AC, 0x8C4, 0x864 and 0x848. The tree sender
   does not serve as-is: depending on the width it fills only one SC field (`BW_40M_SC` at 40 MHz,
   `BW_80M_SC` at 80 MHz, `hal_com.c:11996-12005`), so at 80 MHz it leaves `BW_40M_SC` at 0 and the
   firmware cannot distinguish 149 from 153. A sender filling both fields is needed.
4. Maintaining the driver state by hand in the same order as `set_channel_bwmode`:
   `rtw_set_oper_ch/bw/choffset` (the pattern of `rtw_tdls.c:1012-1015`) and the `pHalData->current_*`
   (center channel 155, width 80, SCs), from which phydm hangs (`hal_dm.c:443-444`); respect
   `dvobj->setch_mutex` against any other switch.
5. TX power does not need to be reloaded (the center does not change; the `awdl_keep_txpwr` guard of the
   tree already avoids that in the software path) and IQK is not triggered.
6. What the offload does not cover remains the driver's: null PM=1/PM=0, TX queue blocking
   (`offch_state`) and phydm restoration on the next tick.
7. Do not mix with a software switch in flight: the firmware neither consults nor updates the
   driver state, and a switch via `set_channel_bwmode` immediately afterwards would redo the work.

The 0x87 does not serve for this (fixed width 20, §6). The read verification of the 0x87 (RF 0x18
against the requested channel) suggests the firmware considers the RF 0x18 write effective
immediately; the 0x1C does not replicate it `?`.

## 11. What to measure on hardware

1. End-to-end latency of the 0x1C (H2C → C2H 0x10 visible through the USB path) for 155/80 with SC40
   alternating 1/2, against the driver measurement (`rtlRadioAwdlOffch` already logs
   "radio to channel ... took N us"): close the cost of the six RF write delays (§4).
2. Spectrum/frames while the offload switches: confirm that with byte 0 = 155 the link of 153/80
   is maintained and that the effective primary moves to 149 (AWDL RX in the slot).
3. TX power and EVM on 149 and 153 after repeated offload switches without reload (same
   test of `AWDL_MCC_TIEMPO_COMPARTIDO.md` §8.4, now through the firmware path).
4. That `c2h_handler_8814a` never sees garbage in the plen=7 of the C2H 0x10 with the packet-based
   parsing (the six residual bytes must not confuse any future handler).

Method and artifacts: firmware IRAM/DMEM disassembly base with the DMEM tables as
references, reading of the two handlers, of `sub_800070FC`/`sub_800070B4` and of their whole
RF/BB access chain, and direct contrast with the vendor tree source. Dumps and base in
`~/re_cache/ida/task_fw_chsw/`; the previous full disassembly remains in
`~/re_cache/ida/task_fw_mcc/`.
