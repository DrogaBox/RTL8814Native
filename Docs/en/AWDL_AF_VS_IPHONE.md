# Own AWDL frames versus iOS 26 frames

Field-by-field comparison of what `RTLAwdInterface` emits (`buildAf`, `putChanSeq`, `queueData`)
against the owly iOS 26 captures (`~/Desktop/AWDLLab/refs/owly/tests/captures`,
`discovery-`, `send-` and `receive-ios26.x-mt7925e-ch44-1.pcap`; 4893 action frames and 4424 data
frames). In the captures the MACs are anonymized (`02:00:00:00:00:0N`). Frames with a channel in the
radiotap header (44) come from the iPhone; those without it come from the owly node itself. Dumping
with `tools/awdl_af_extract.py` from the lab and an ad hoc TLV decoder.

## 1. Fixed header and TLV order

16 B header identical: `7f 00 17 f2 08 10 <subtype> 00`, plus the physical TX time and the
target (u32 LE). PSF = subtype 0, MIF = subtype 3.

| Frame | iPhone (iOS 26, channel 44) | Own |
|---|---|---|
| PSF | `4, 5, 18, 6, 24, 12, 7, 17, 21` | `4, 5, 18` + the 151 template |
| MIF | `4, 5, 18, 24, 12, 7, 17, 16, 21, 6, 2×6` | `4, 5, 18` + the MI template from requests 149/225 (since `6fd22ef`) |

`AWDL_TX_FRAME_REVIEW.md` §5 already checked that the sync template (request 151) provides
`6, 24, 12, 7, 17, 21` in that order. Up to `6fd22ef` the MIF reused that template, so it was
missing TLV 16 (ARPA, host name) and TLV 2 (service responses), and 6 went in the
PSF position (H4 of that review). The kernel composes those TLVs into the *master
indication* template (`buildMasterIndicationTemplate`, `AWDL_PSF_MIF_EMISSION.md` §3.2), which it delivers with the
requests 225 (permanent) and 149 (immediate send). Since `6fd22ef` each MIF carries that template
after `4, 5, 18`, and the order matches the iPhone's. Neither the kernel nor owly require those TLVs to register the
sender, but AirDrop needs the ARPA and the services.

## 2. TLV 4 (sync params, length 73)

Same fixed values as the iPhone: AW 16 TU (`10 00`), PSF period 110 TU (`6e 00`), `00 18`,
extended and common AW 16, `03 03 03 03`, presence mode 4, AW counter at +29 and AP alignment
0, with 2 B of padding at the end.

There are two differences:

- **+15, TU remaining of the AW.** The iPhone sends 0 in its PSFs, and owly computes what remains of the
  first AW of the slot: 0 in the other three (`tlv.rs`, `AW_PERIOD_TU - elapsed`). The code
  was sending what remained of any AW in progress (1..16), so a peer would have seen an open window
  during the extension AWs. Fixed in `020c32d`: non-zero value only inside the
  first AW of the slot.
- **Embedded sequence.** The iPhone uses encoding 1 (u16 per slot with band tag) and owly
  and the project's own code use 3 (channel and class pairs). The kernel's embedded parser accepts both
  (`AWDL_TLV18_OPCLASS_PARSER.md` §4). No change.

## 3. TLV 5 and TLV 18

- TLV 5 (21 B): distance at +3, master at +5, master metrics at +11 and own at +15. Same
  layout as the iPhone; owly sends distance 1 and the iPhone master 0.
- TLV 18 (41 B): `0f 03 00 03 ff ff` + 16 pairs + 3 B of padding, same as the iPhone. The iPhone
  announces `{44,128}` in its 5 GHz slots and `{6,81}` in a 2.4 GHz one; the code, `{ch,128}` or
  `{ch,81}` in the served slots and `{0,0}` in the rest. The kernel accepts both forms
  (`AWDL_TLV18_OPCLASS_PARSER.md` §6).

## 4. Data frames

| Field | iPhone | Own |
|---|---|---|
| Frame control | `0x08` (data) and `0x88` (QoS, TID 0, no A-MSDU) | `0x08` |
| A3 | `00:25:00:ff:94:73` | same |
| Body | `aa aa 03 00 17 f2 08 00 03 04 <seq LE> 00 00 <ethertype BE>` | same |

There is no A-MSDU in the captures. Own RX accepts data and QoS, with or without the retry bit
(`deliverData`, mask `0x47` over the flags byte).

## 5. Own frames captured

On 2026-11-05, with `Tools/iphone_test.sh`, the Mac's frames could for the first time be compared at the radio, not
just the code: in one minute, 400 PSFs of 298 B and 129 MIFs of 958 B, with no peer.

| Frame | iPhone | Mac |
|---|---|---|
| PSF | `4, 5, 18, 6, 24, 12, 7, 17, 21` | `4, 5, 18, 6, 24, 12, 21` |
| MIF | `4, 5, 18, 24, 12, 7, 17, 16, 21, 6, 2×6` | `4, 5, 18, 24, 12, 16, 21, 6, 2×6` |

Only TLVs 7 and 17 were missing: the layer did not answer the requests they come from
([AWDL_CAPS_ADVERTISED.md](AWDL_CAPS_ADVERTISED.md) §6, §10). §1 assumed the template provided them.

Other differences, unchanged for now:

| Field | iPhone | Mac |
|---|---|---|
| TLV 21 | `a0 02` | `a0 01`: device class, 1 is macOS |
| TLV 12, extended | `7d 11` | `2d 00` |
| TLV 12, +5 | `03 00` | `07 00` |
| TLV 6 | `00 00 00 d7 00 30 02 00 80 01 20 20 08` | `00 00 00 07 00 30 44 00 00 01 20 08 01` |

The meaning of TLV 6 and whether any of these differences matter to an iPhone are `?`.

## 6. Pending

- The real template the manager delivers is dumped in hex into `RTLAwdlLog` since
  `020c32d`. With it, the project's own TLV 21 (version) can be compared against the iPhone's `a0 02` and the
  sizes of 6, 12, 7 and 17.
- The MI template the manager delivers (length and content) is in `RTLAwdlLog` since
  `6fd22ef` ("MI template N bytes"); still not seen on hardware.
