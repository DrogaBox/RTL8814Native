# The RTL8814A firmware and the MCC H2C commands

Closes the `?` of `AWDL_MCC_TIEMPO_COMPARTIDO.md` §2: whether the firmware the driver downloads to
the RTL8814AU implements the H2Cs of MCC mode (`hal_com_h2c.h`: `H2C_MCC_LOCATION` 0x10,
`H2C_MCC_RQT_TSF` 0x15, `H2C_MCC_MACID_BITMAP` 0x16, `H2C_MCC_CTRL_V2` 0x17, `H2C_MCC_CTRL`
0x18, `H2C_MCC_TIME_SETTING` 0x19, `H2C_MCC_IQK_PARAM` 0x1A) and the `C2H_MCC` 0x17. The
blob is inspected statically, without hardware. Firmware addresses are given as offsets
within the extracted sections (the disassembly database uses IRAM at 0x80000000 and
DMEM at 0x80200000). Anything unverified is marked `?`.

**Short answer: no.** The H2C dispatcher of the firmware ignores 0x10 and 0x15–0x1A (they fall to
the default path, which only sets an error bit) and no path emits an MCC `C2H_MCC`.
The `CONFIG_MCC_MODE` path of `hal_mcc.c` would be dead with this blob; the manual per-slot path
of `AWDL_MCC_TIEMPO_COMPARTIDO.md` §7 is the only viable one without changing firmware.

## 1. What image the driver downloads and how

`FirmwareDownload8814A` (`Source/rtl8814au/hal/rtl8814a/rtl8814a_hal_init.c`) takes
`array_mp_8814a_fw_nic` (line 734; it only uses `array_mp_8814a_fw_wowlan` if `bUsedWoWLANFw`),
reads the header with the `GET_FIRMWARE_HDR_*_3081` macros (64 bytes) and copies by IDMA
(`IDDMADownLoadFW_3081`) the DMEM section (from offset 64) to `OCPBASE_DMEM_3081` (0x00200000)
and the IRAM section (at the end of the file) to `OCPBASE_IMEM_3081` (0x00000000), each with 8
bytes of checksum at the end (`FW_CHKSUM_DUMMY_SZ`). The MCU boot waits for
`DMEM_CHKSUM_OK`/`IMEM_CHKSUM_OK` in `REG_8051FW_CTRL_8814A` (0x80) and `CPU_DL_READY` (BIT15).
The register name inherits the "8051"; the firmware code is not (§3).

## 2. The extracted image

| Header field (64 B) | Value |
|---|---|
| Signature | 0x8814 |
| Category / function | 0x00 / 0x00 |
| Version / subversion / index | 33 / 6 / 0 |
| Date | 2019-06-11 21:15 |
| Memory usage (byte 24) | 0x08 (IRAM only, without its own boot loader) |
| `TOTAL_DMEM_SZ` (offset 36) | 0x1698 (5784 B) |
| `IRAM_SZ` (offset 48) | 0xF3F8 (62456 B) |
| Total size | 68320 = 64 + (5784+8) + (62456+8) ✓ |
| Words 32/56/60 | 0x80200000 / 0x80121000 / 0x80000000 |

The words 0x80200000 and 0x80000000 match those of the firmware of the same generation of
RTL8822B (`rtw8822b_fw.bin` from linux-firmware, v30 of 2020), whose header is identical field
by field; the two blobs do not share a single 64-byte code block, so the comparison with
the 8822B provides no handlers, it only confirms the image format.

Dumps: `~/re_cache/ida/task_fw_mcc/fw_nic.bin`, `fw_nic_dmem.bin` (5792 B), `fw_nic_iram.bin`
(62464 B) and `fw8814.i64` (disassembly database, 425 functions; ~12 KB of IRAM remain
undecoded, `?`, but the whole H2C/C2H path is within what was analyzed).

## 3. The processor is a small MIPS with MIPS16e code

It is not an 8051: there is not a single `MOV DPTR,#imm16` toward register space nor the
8051-specific patterns in 62 KB. What there is:

- DMEM tables full of kseg0 pointers (`0x8000xxxx`) with bit 0 at 1 — the MIPS16 ISA bit —
  pointing inside the IRAM (exception vectors at DMEM 0x08–0x40, and task tables at
  0x5C, 0x84, 0xB8, 0xCC, 0xE0, 0xF4, 0x110, 0x158, 0x24C, 0x26C, 0x2A8, 0x2BC,
  0x314, 0x398).
- The MIPS16e NOP (halfword 0x6500, LE `00 65`) among the most frequent halfwords.
- Register accessors with variable base: DMEM 0x4BC stores 0xB8600000 and 0x4C0 stores
  0xB8640000; the generic accessors `sub_80006AAC` (read: `base + (reg & 0xFFFF)`),
  `sub_80006AE4` (write with field mask) and `sub_80004478`/`sub_800058F0` (by bytes)
  add the offset to that base. The write of `0x8000A640` chooses 0xB8600000 when
  `reg & 0xFF00` is 0x0000 or 0x1000 and 0xB8640000 otherwise, so 0xB86010F8 (§5, §6) is
  MAC register 0x10F8 `?` (unnamed in the driver tree).
- The DMEM descriptors are (offset, base) pairs that match real registers of the driver tree:
  (0x550,B864)=`REG_BCN_CTRL`, 0x551=`REG_BCN_CTRL_1`, 0x554=`REG_MBSSID_BCN_SPACE`,
  0x558=`REG_DRVERLYINT`, 0x6A8=`REG_BCN_PSR_RPT`, 0x4A4=`REG_ARFR5`, 0x44C=`REG_ARFR1`,
  0x1C0=`REG_MCUTST_1`, 0x1C7=`REG_MCUTST_WOWLAN`, 0x1000=`REG_SYS_CFG3` and (0x80,B860)=
  `REG_8051FW_CTRL`.

Besides B8600000/B8640000 there are windows B8660000, B8700000 and B8780000, spaced 0xA0000 and
0x80000 just like the driver's `OCPBASE_RPTBUF`/`RXBUF`/`TXBUF` (0x18660000/0x18700000/0x18780000):
they are the report, RX and TX buffers as seen by the MCU. `?` the exact mapping B8600000↔0x18600000.
If the chip also carries an 8051 that only runs ROM (FWDL and queue), nothing in this image shows
it.

## 4. The H2C path: queue in DMEM, not mailbox reading

The driver writes the command in the usual mailbox (`FillH2CCmd_8814`, `rtl8814a_cmd.c:60`:
`REG_HMEBOX_0..3` = 0x1D0–0x1DF with id in byte 0 and 3 payload bytes, extension in
0x1F0–0x1FF) and waits for the hardware to clear the bit of `REG_HMETFR` (0x1CC).

The loaded firmware reads none of those registers: no immediates 0x1A0–0x1FF in the 425
functions, no descriptors (0x1D0–0x1DF and 0x1F0–0x1FF do not appear in the DMEM), no absolute
words 0xB86x01D0/0xB86x01A0. What there is:

- A command queue in the DMEM: 8-byte entries `[id][7 args]` starting at
  0x802017C7 + 8·i (the firmware database is at 0x802016B0), with head
  (0x80201807) and tail (0x80201808) indices, reset by the init `sub_80001614` and `sub_8000172C`.
- The drainer `sub_80005834`, part of the main loop `sub_800016EC`, takes out the entry whose
  id it has and calls the dispatcher `sub_80005668` with (id, args).
- Who fills the queue is not in the image: the producer is the hardware itself (mailbox DMA
  into the queue) or the MCU ROM `?`. It is consistent with the hardware clearing the HMETFR bit
  the driver waits for, and with this generation (8192F/8822B) using a "per-packet" H2C.

## 5. The dispatcher `sub_80005668`: complete id map

Comparison tree over the id byte (addresses = IRAM). "Default" = `loc_8000581C`:
sets BIT0 in 0xB86010F8 (pointer in DMEM 0x4E4; the same register where the C2H sender
sets BIT9 when its ring is full, §6) and does nothing else. `loc_8000582C` = silent
exit, without an error bit. Before the tree, ids 0x40–0x5F all go to the DM class
(`(id − 0x40) & 0xFF < 0x20` → `loc_8000B2DC`).

| Id (driver) | Handler | Note |
|---|---|---|
| 0x00 `RSVD_PAGE` | 0x800054E4 | template positions (probe rsp, ps-poll, null, QoS null) |
| 0x01 `MEDIA_STATUS_RPT` | 0x80005528 | |
| 0x0B | 0x80001040 | 8814A id with no equivalent in `hal_com_h2c.h` `?` |
| 0x14 | silent exit | |
| **0x10 and 0x15–0x1B** | **default** | **includes the seven MCC ids (0x1B is not MCC but falls the same)** |
| 0x1C `CHNL_SWITCH_OPER_OFFLOAD` | 0x8000812C | channel/width via `sub_800070FC`/`0x800070B4` + C2H 0x10 |
| 0x1D `SINGLE_CHANNELSWITCH_V2` | 0x80008128 | empty body (`jr ra`) |
| 0x1E | 0x80000FE4 | emits C2H 0x20 |
| 0x1F | 0x80000E80 | config via register fields |
| 0x20 `SET_PWR_MODE` | 0x80005500 | firmware LPS/SmartPS |
| 0x21, 0x23, 0x25 | silent exit | `PS_TUNING_PARA`, `P2P_LPS_PARAM`, `PS_SCAN_ENABLE` accepted with no effect |
| 0x22, 0x26 | default | |
| 0x24 `P2P_PS_OFFLOAD` | 0x8000116C | |
| 0x27 `INACTIVE_PS` | 0x80001130 | |
| 0x2C `DEFAULT_PORT_ID` | 0x80001060 → 0x800059C0 | per-port context (multi-port) |
| 0x40–0x4B (DM class) | 0x8000B2DC → `jr` through the DMEM table 0x110 (index `id − 0x40`) | 0x40 `MACID_CFG`→0xABB0, 0x41 `TXBF`→0x77FC, 0x42 `RSSI_SETTING`→0xB108, 0x43 `AP_REQ_TXRPT`→0xA7FC, 0x46 `RA_MASK_3SS`→0xB2B4, 0x47 `RA_PARA_ADJUST`→0xAE7C, 0x48 `DYNAMIC_TX_PATH`→0xAAFC, 0x49 `FW_TRACE_EN`→0xB260, 0x4B→0xB020 (followed by 0xA7FC `?`); 0x44, 0x45 `IQ_CALIBRATION` and 0x4A point to the exit |
| 0x4C–0x5F | silent exit of the DM class | (includes `REQ_PER_RPT` 0x4E) |
| 0x60/0x61 BT | 0x80004974 / 0x80004A18 | |
| 0x62–0x68 BT | 0x4A04 / 0x49F0 / 0x4934 / 0x4708 / 0x49DC / 0x49AC / 0x4998 | |
| 0x69 `BT_WIFI_CTRL` | 0x80004A2C | |
| 0x6A/0x6B | default | (no BT_FW_PATCH nor BTC_WL_PORT_ID) |
| 0x6C–0x71 BT | 0x4984 / 0x473C / 0x47F4 / 0x4830 / 0x4880 / 0x4860 | |
| 0x87 `CHNL_SWITCH_OFFLOAD` (WoWLAN) | 0x8000817C | channel + verification + C2H 0x10 |
| 0xC5 `BCNHWSEQ` | 0x800053EC | the one the driver uses for the HW beacon seq |
| rest (0x02–0x0A, 0x0C–0x13, 0x28–0x2B, 0x2D–0x3F, ≥0x72 except 0x87/0xC5) | default | |

The DM class names are those of the driver enum for that id; each handler function
was not analyzed (`?`). What matters here does not change: the MCC ids never reach any
handler.

## 6. The C2H: ring in DMEM and emitted ids

The common sender is `sub_80004CC8`: sets BIT2 in the status word 0x8020173C
(0x802016B0 + 0x8C) and copies 16 bytes to the event ring at 0x802016B0 + 0x15A + 16·i (read
index at 0x80201D0A, write at 0x80201D0B, 80 entries, wrap at 0x50). If the ring
is full it copies nothing and sets BIT9 (0x200) in 0xB86010F8: it is an overflow warning, not the
normal notification. The buffer format of each caller is the classic
[id][seq][len][payload...]. `?` how the slot travels to the host (on USB the driver receives the
C2H as packets through the RX path).

Ids emitted by the 12 callers of `sub_80004CC8` (byte 0 of the buffer, constant except where
noted):

| Id | Name in the driver | Caller |
|---|---|---|
| 0x02 | `C2H_TXBF` | `sub_800075E4` |
| 0x04 | `C2H_AP_REQ_TXRPT` | function at 0x8000A9A0 (no bounds in the base) |
| 0x09 | `C2H_BT_INFO` | `sub_80008910` |
| 0x0A | — `?` | `sub_800088DC` |
| 0x0B | `C2H_BT_MP_INFO` | `sub_80008878` |
| 0x10 | `C2H_FW_CHNL_SWITCH_COMPLETE` | `sub_8000812C` (H2C 0x1C) and `sub_8000817C` (H2C 0x87) |
| 0x15 | `C2H_MAILBOX_STATUS` | function at 0x8000886E (no bounds in the base) |
| 0x1E | `C2H_BCN_EARLY_RPT` | `sub_80002884` |
| 0x20 | — `?` | `sub_80000FE4` (H2C 0x1E) |
| 0x21 | — `?` | `sub_80000F0E` (carries 0x478/0x484/0x4C8/0x4D4 and internal state) |
| several | 0x00 `C2H_DBG`, 0x0C `C2H_RA_RPT` and others taken from structures `?` | wrapper `sub_8000A684` (8 callers, DM class paths) |

None writes 0x17 as a constant id; the wrapper ids that come out of structures were not
followed (`?`).

**The 0x17 that appears in the firmware is not a C2H.** H2C 0x62 (`FORCE_BT_TXPWR`) calls
`sub_80004A04` → `sub_800089E0(0x17, 1, args)`. That composer does not use the ring: after a
switch by id (0x11, 0x1B, 0x23, 0x28, 0x30 touch flags at 0x80201B90/91) and a
check of `*(u16 *)0x80203240 & 0x2AA`, it calls `sub_80008760`, which writes the id to
0xB8003400, the length to 0xB8003401 and the payload from 0xB8003402, and triggers with BIT0 of
0xB8003408 (pointers in DMEM 0x8D4–0x8E0). It is another window, outside the MAC registers, and
only the BT handlers use it (0x62–0x68, 0x6C, ids 0x11, 0x17, 0x1B, 0x23, 0x28, 0x30, 0x34,
0x38): most likely the mailbox toward the BT core `?`. With MCC compiled,
`rtw_hal_mcc_c2h_handler` would never receive a C2H 0x17. The 8814A driver
(`c2h_handler_8814a`, `rtl8814a_cmd.c:725`) only handles 0x02/0x03/0x04/0x0D on its own; the
rest goes to the generic one (`hal_intf.c:1242` is the `C2H_MCC` branch).

## 7. Direct answer to the MCC question

| Command | Id | Does firmware 33.6 implement it? | Evidence |
|---|---|---|---|
| `H2C_MCC_LOCATION` | 0x10 | No | dispatcher: default (`loc_8000581C`) |
| `H2C_MCC_RQT_TSF` | 0x15 | No | default |
| `H2C_MCC_MACID_BITMAP` | 0x16 | No | default |
| `H2C_MCC_CTRL_V2` | 0x17 | No | default |
| `H2C_MCC_CTRL` | 0x18 | No | default |
| `H2C_MCC_TIME_SETTING` | 0x19 | No | default |
| `H2C_MCC_IQK_PARAM` | 0x1A | No | default |
| `C2H_MCC` | 0x17 | No (the only 0x17 is a BT mailbox message, not a C2H) | §6 |

Compiling `CONFIG_MCC_MODE + CONFIG_CONCURRENT_MODE` and adding `hal/hal_mcc.c` would have no
functional effect: the seven H2Cs would be swallowed by the default (with the error bit set in
0xB86010F8) and the C2H 0x17 with the MCC state would never arrive. The cheap hardware test
`AWDL_MCC_TIEMPO_COMPARTIDO.md` §7.4 proposed is answered without spending it.

## 8. What the firmware offers that does matter for AWDL

- **Firmware channel switching**: H2C 0x1C (`CHNL_SWITCH_OPER_OFFLOAD`, args with channel,
  width and offset packed) and 0x87 (WoWLAN variant, with a verification read of the new
  channel), both closing with `C2H_FW_CHNL_SWITCH_COMPLETE` (0x10). This is the "switch offload"
  that `AWDL_MCC_TIEMPO_COMPARTIDO.md` §3.3 discarded because of `RegFWOffload == 2`; it exists
  in the firmware, although the tree driver does not use it and its real latency is `?` (measure
  before predicting). The 0x1D, in contrast, is an empty stub.
- **LPS/SmartPS** (H2C 0x20): the firmware generates by itself the saving null/PS-Poll frames over
  the `RSVD_PAGE` (0x00) templates. It serves PM advertising on the home channel, not a slot
  schedule: the per-slot off/on remains the driver's task.
- **P2P_PS_OFFLOAD** (0x24) and **DEFAULT_PORT_ID** (0x2C) with per-port context switching
  (`sub_800059C0`): the multi-port skeleton exists, with `?` on whether it syncs TSF per port.
- **Firmware IQK** (0x45), MACID_CFG (0x40), RSSI_SETTING (0x42), AP_REQ_TXRPT (0x43),
  RA_MASK_3SS (0x46), DYNAMIC_TX_PATH (0x48), BCNHWSEQ (0xC5).
- **Beacon early interrupt**: the firmware programs `REG_DRVERLYINT` (0x558, descriptor in
  DMEM 0x400) and emits `C2H_BCN_EARLY_RPT` (0x1E); the early beacon warning mechanism is
  available to sync slots against the AP DTIM.
- **TSF**: the firmware touches its own TSFs (0x560–0x57F appear in descriptors, e.g.
  0x578–0x57A next to `REG_BCN_CTRL`), but offers the host no TSF read without MCC:
  `H2C_RQT_TSF` (0x15) is not handled. The host keeps reading its own TSF registers as until
  now (`AWDL_RTL8814_HW_SUPPORT.md`).

## 9. Verdict

The firmware the T9UH downloads (v33.6, 2019-06-11) does not implement the MCC block of this
generation: the seven H2Cs (0x10, 0x15–0x1A) fall to the default with the error bit and no path
emits a `C2H_MCC`: the only 0x17 is a BT mailbox message. For AWDL time sharing while the station
is associated, the MCC firmware path is discarded with this blob; the manual per-slot path
(`AWDL_MCC_TIEMPO_COMPARTIDO.md` §7.1–7.3) remains, and, as a future line conditioned on
measuring, the 0x1C/0x87 switching offload the firmware does carry.

Method and artifacts: extraction of the C array to binary, slicing by the layout of the driver
itself, MIPS/MIPS16e disassembly database of IRAM and DMEM with the DMEM tables converted into
xrefs, and reading of the dispatcher, the queue and the C2H sender. Everything stays in
`~/re_cache/ida/task_fw_mcc/` (`fw8814.i64`, `all_disasm.txt`, `fw_nic*.bin`, log of each pass)
and outside the repository.
