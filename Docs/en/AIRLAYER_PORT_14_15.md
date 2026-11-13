# Porting RTL8814AirLayer to macOS 14.8.9 (23J631) and 15.8.2 (24H232)

## 1. Scope and method

Goal: determine what `RTL8814AirLayer` needs in order to run on the IO80211/Skywalk families of
macOS 14.8.9 (23J631) and 15.8.2 (24H232), taking 25G241 (25.6.0) as the current working reference.

Analyzed artifacts (identity in the `KC.md5`/`KC.sha256` of each directory):

| build | KC | md5 | dumps |
|---|---|---|---|
| 23J631 | local copy `boot_23J631.kc` | `2ab1c3ac3db747642bf4cd6ac73e12f5` | `Docs/re/io80211_23J631/` |
| 24H232 | local copy `boot_24H232.kc` | `bcf4c23aa5f52f69951aea31c044f5de` | `Docs/re/io80211_24H232/` |
| 25G241 | `/System/Library/KernelCollections/BootKernelExtensions.kc` | `f489ff8be80e6013a1bf47561dcaef45` | `Docs/re/io80211_25.6.0/` |

The two new directories follow the layout of `Docs/re/io80211_25.6.0/` (the six symbol dumps per
fileset, `classes.txt`, the `vt_*.txt` files and the KC hashes). The reference gains the dumps
`vt_IO80211VirtualInterface.txt`, `vt_IO80211AWDLProtocol.txt` and
`vt_AppleBCMWLANProximityInterface.txt`, which were missing and are needed for the AWDL block.

Procedure and validation:

- Vtables decoded from each KC with the same chained-fixup arithmetic as the reference. The
  base is re-derived per build from the collection headers: the lowest vmaddr is
  `0xffffff8000100000` in all three builds, and with it 100 % of the cells of the 13 relevant
  vtables land on a defined symbol (excluding `IOService`, whose cells point to internal kernel
  functions with no entry in the symtabs, same as in the reference dump).
- The pipeline was validated by reproducing byte for byte the 12 existing `vt_*.txt` dumps of
  25.6.0 before generating those of 23J631 and 24H232.
- Each symbol dump has exactly as many lines as its symtab announces.
- Instance sizes and constructors verified with headless disassembly over copies of the analysis
  databases of each KC (nothing was run against the originals).

Convention: cell numbers are the indices of the `vt_*.txt` dumps (slot × 8 = offset in bytes from
the start of the object's vtable, `ZTV+0x10`). The names of the pure cells of an abstract class
come from the vtable of the Apple implementation that resolves them in that build
(`AppleBCMWLANCore`, `AppleBCMWLANSkywalkInterface`/`AppleBCMWLANInfraProtocol`,
`AppleBCMWLANProximityInterface`).

## 2. Verdict: one header and one build for 23J631 and 24H232?

**No.** 23J631 (Sonoma) and 24H232 (Sequoia) each need their own header, stubs and binary:

1. The WCL block of `IO80211InfraProtocol` is relocated between the two: the same methods change
   slot and also relative order (the generic setters go down −47/−68, e.g. `setWCL_SCAN_REQ`
   659 → 592; the status getters −2/−32; the channel getters go up +7/+12). From 24H232 to 25G241
   the block moves again, +3/+10 depending on the subgroup.
2. The set of pure cells of `IO80211InfraProtocol` differs: 231 in 23J631, 191 in 24H232, 200 in
   25G241. In 23J631 the set includes ~96 classic `apple80211` ioctls (`setSCAN_REQ`, `setASSOCIATE`,
   `getSSID`, `getBSSID`, `getSTATE`, `setROAM`, `setDEAUTH`, …) that a subclass must answer there
   mandatorily and that no longer exist in 24H232.
3. Several methods the layer overrides do not exist in 23J631: `getPLATFORM_CONFIG` and `getLogger` on
   the controller, the setters `setWCL_QOS_PARAMS`, `setINFRA_ENUMERATED`, `setBSS_BLACKLIST`,
   `setWCL_SOI_CONFIG`, `setWCL_UPDATE_FAST_LANE` on InfraProtocol, and `setMacAddress(mloAddrArray&)`.
4. Its own block of `IO80211VirtualInterface` starts at slot 446 in 23J631 and at 460 in
   24H232; 23J631 additionally has three mbuf methods that disappear in Sequoia
   (`outputPacket(__mbuf*, void*)`, `inputPacket(__mbuf*, packet_info_tag*)`,
   `reportTransmitStatus(__mbuf*, int, packet_info_tx*)`).
5. The pure block of `IO80211AWDLProtocol` has 62 cells in 23J631 and 64 in 24H232:
   `setAWDL_CONFIG` and the pure `setMacAddress(ether_addr&)` are missing.
6. There are signature changes (§6), so even a header matched "by name" would not be safe.

The current header (`IO80211_25_6_0.h` + `AWDLInterface.h`) is also not valid for 24H232: besides the
slots, the instance sizes of the interface chain differ (§3) and the structural gate would reject it
before creating anything.

Caveat about the gate: the instance sizes of the eight classes are **identical** between
23J631 and 24H232, so the structural check (sizes + superclasses) does not distinguish these
two builds. The validated-builds list (`Tools/airlayer_builds.txt`) is what prevents loading a
binary of one into the other.

## 3. Instance sizes and origin of the 16 bytes

Sizes measured on the three analysis databases and confirmed in the meta-class constructor of each build
(`OSMetaClass::OSMetaClass(char const*, OSMetaClass const*, unsigned int)`; the third argument is
loaded into `ecx` just before the call):

| class | 23J631 | 24H232 | 25G241 | Δ |
|---|---|---|---|---|
| `IOSkywalkInterface` | 0xB0 | 0xB0 | 0xB0 | — |
| `IOSkywalkNetworkInterface` | 0xD0 | 0xD0 | 0xE0 | +0x10 |
| `IOSkywalkEthernetInterface` | 0x110 | 0x110 | 0x120 | +0x10 |
| `IO80211SkywalkInterface` | 0x118 | 0x118 | 0x128 | +0x10 |
| `IO80211InfraInterface` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211InfraProtocol` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211VirtualInterface` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211AWDLProtocol` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211Controller` | 0x128 | 0x128 | 0x128 | — |

Disassembly evidence (all addresses are instruction heads seen in the
headless dump):

- `IOSkywalkInterface`: 23J631 `0xffffff80028591c3` (`mov ecx, 0xB0h`), 24H232
  `0xffffff8002975ba3` (`mov ecx, 0xB0h`), 25G241 `0xffffff8002afb9a3` (`mov ecx, 0xB0h`).
  Meta-constructors at `0xffffff80028591ac` (23J631), `0xffffff8002975b8c` (24H232),
  `0xffffff8002afb98c` (25G241).
- `IOSkywalkNetworkInterface`: 23J631 `0xffffff80028654d1` (`mov ecx, 0xD0h`), 24H232
  `0xffffff8002983995` (`mov ecx, 0xD0h`), 25G241 `0xffffff8002b09ce9` (`mov ecx, 0xE0h`).
  Meta-constructors at `0xffffff80028654ba` (23J631), `0xffffff800298397e` (24H232),
  `0xffffff8002b09cd2` (25G241).

The +0x10 lands only in `IOSkywalkNetworkInterface`; the six subclasses inherit it and neither
`IOSkywalkInterface` nor `IO80211Controller` changes. Comparing the `init(OSDictionary*)` of that
class in the three builds shows the same two ivars at different positions:

| ivar written in `init` | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| expansion pointer (`IOMallocTypeImpl`; inside it, two `IOLockAlloc` at +0x10/+0x18 and a
`thread_call_allocate_with_options` at +0x28) | +0xB8 | +0xB8 | +0xC0 |
| byte flag set to zero | +0xC0 | +0xC8 | +0xD8 |

Evidence: 23J631 `init` at `0xffffff80028655a0`, stores at `0xffffff80028655c7`
(`mov [rbx+0xB8h], rax`) and `0xffffff800286561c` (`mov byte ptr [rbx+0xC0h], 0`); 24H232 `init` at
`0xffffff8002983a64`, stores at `0xffffff8002983a8b` and `0xffffff8002983ae0` (+0xB8h and +0xC8h);
25G241 `init` at `0xffffff8002b09dac`, stores at `0xffffff8002b09dd3` and `0xffffff8002b09e28`
(+0xC0h and +0xD8h).

Conclusion: the 16 new bytes of Tahoe are the gap that separates the expansion pointer from the flag
(additional +0xB8…+0xBF and the shift of the byte from +0xC8 to +0xD8); both ivars already exist in Sonoma
and Sequoia, with the same role in init and only their placement changes. The bytes in between are
not initialized in `init` (?). For the layer, which declares those ivars as opaque padding, the practical
effect is the instance size the gate already uses.

## 4. Vtables per build: counts and structural changes

Cells per vtable (not counting the trailing null cell) and `__cxa_pure_virtual` cells:

| class | 23J631 | 24H232 | 25G241 | pure 23/24/25 |
|---|---|---|---|---|
| `IOSkywalkInterface` | 282 | 282 | 282 | 0/0/0 |
| `IOSkywalkNetworkInterface` | 332 | 332 | 333 | 0/0/0 |
| `IOSkywalkEthernetInterface` | 353 | 352 | 353 | 0/0/0 |
| `IO80211SkywalkInterface` | 446 | 460 | 461 | 0/1/0 |
| `IO80211InfraInterface` | 451 | 465 | 469 | 0/1/1 |
| `IO80211InfraProtocol` | 682 | 655 | 668 | 231/191/200 |
| `IO80211VirtualInterface` | 485 | 500 | 505 | 0/1/1 |
| `IO80211AWDLProtocol` | 547 | 563 | 568 | 62/64/64 |
| `IO80211Controller` | 471 | 463 | 463 | 10/12/12 |

Changes that alter numbering, per transition:

- 23J631 → 24H232:
  - `IOSkywalkNetworkInterface`: reordering with the same number of cells:
    `registerNetworkInterfaceWithLogicalLink` and `deregisterLogicalLink` move up from 319/320 to 282/283
    and the contiguous pure cell goes with them; the BSD block (`prepareBSDInterface` …
    `reportDetailedLinkStatus`) shifts +2.
  - `IO80211SkywalkInterface` (+14): the peer management family comes in
    (`attachPeer`, `createPeer`, `detachPeer`, `findPeer`, `getController`, `getDataPathInterfaceStats`,
    `getDataPathPeerStats`, `getDataQueueDepth`, `getLastQueuePacketTime`, `getLastRxTimeStamp`,
    `getLastRxUnicastLinkActivityTime`, `getLastTxTimeStamp`, `getNClearTxRxLatency`, `logRxLatency`,
    `logTxLatency`, `setDebugTrafficReport`, `setUserBufferInfo`, `updateInterfaceDataStats`,
    `updatePeerDataStats`); `enabledBySystem`, `getCurrentBssid`, `setEnabledBySystem`,
    `setSelfMacAddr`, `willRoam` and the inherited copy of `registerNetworkInterfaceWithLogicalLink` go out.
  - `IO80211InfraInterface` (+14): the same plus `createLQMData` and `setCurrentApAddress`; additionally
    `setCurrentBssid` and `setPoweredOnByUser` go out.
  - `IO80211InfraProtocol` (−27): the pure set goes from 231 to 191. The ~96 classic ioctls go out
    (`setSCAN_REQ`, `setASSOCIATE`, `setDEAUTH`, `getSSID`, `getBSSID`, `getSTATE`, `setROAM`,
    `getRSSI_BOUNDS`, `getRX_RATE`, `getEXTENDED_STATS`, …) and ~57 new methods come in
    (`setWCL_QOS_PARAMS`, `setWCL_SOI_CONFIG`, `setWCL_UPDATE_FAST_LANE`, `setINFRA_ENUMERATED`,
    `setBSS_BLACKLIST`, `setWCL_ARP_MODE`, `setWCL_ASSOCIATED_SLEEP`, `setWCL_WNM_OFFLOAD/OPS`,
    `setFW_CLOCK_SOURCE`, `setTIMESYNC_*`, `setMWS_*`, `setPOWER_PROFILE`, `setIPV4_PARAMS`,
    `setIPV6_PARAMS`, `getHE_COUNTERS`, `getTIMESYNC_STATS`, `getSYSTEM_SLEEP_CONFIG`, …). The common
    WCL block is relocated and reordered (§2.1).
  - `IO80211VirtualInterface`/`IO80211AWDLProtocol` (+15/+16): the VI own block moves from 446 to 460;
    `getPeerSlotDataStats`, `getPeerDataStats`, `clearPeerDataStats` and
    `setAwdlCurrentChannelSequenceIndex` come in (cells 467-470 of 24H232) and the rest of its own block
    shifts from 453-465 to 471-483; the Sonoma mbuf trio goes out (`outputPacket`, `inputPacket`,
    `reportTransmitStatus`, cells 466-468 of 23J631) and `setMacAddress(ether_addr&)` becomes pure
    at slot 415; the AWDL block adds `setAWDL_CONFIG` (62 → 64 pure with it and the `setMacAddress`).
  - `IO80211Controller` (−8): the classic ioctl dispatcher disappears
    (`apple80211_ioctl`/`_get`/`_set`, `apple80211SkywalkRequest`, `apple80211VirtualRequest`),
    `enable`/`disable(IO80211SkywalkInterface*)`, `postMessage`,
    `getHardwareAddressForInterface`, `useAppleRSNSupplicant`, `isAssociatedToMovingNetwork` and
    `setVirtualHardwareAddress`; `CreatePostOffice`, `getPostOffice`, `debugStateInit`,
    `getActionFramePoolCapacity`, `getPLATFORM_CONFIG` and `allocIO80211RecursiveLock` come in. The pure
    ones go from 10 to 12: `handleCardSpecific` drops from 408 to 403, the getter block from 413-420 to
    408-415, and `getLogger` (424) and `getFaultReporterFromDriver` (432) appear as pure.
- 24H232 → 25G241:
  - `IOSkywalkNetworkInterface` (+1): `setRxFlowSteering` (~293); additionally `willTerminate` becomes
    overridden in the class (slot 128, no count change).
  - `IO80211SkywalkInterface` (+1): `setRxDataStallStats`, `setRxFlowSteering`, `syncDPSStats`; the
    4-argument `setLinkState` goes out (its cell is taken by `isChipInterfaceReady`/
    `isDebounceOnGoing`).
  - `IO80211InfraInterface` (+4): the previous ones plus `setWCL_LINK_STATE_UPDATE` (which leaves the
    InfraProtocol pure set in Tahoe).
  - `IO80211InfraProtocol` (+13/−4 pure): `getACCESSORY_STATE`, `getDEVICE_ORIENTATION`,
    `getHE_CAPABILITY`, `getLQM_STATISTICS`, `getP2P_DEVICE_CAPABILITY`, `getPOWERTABLE_VERSION`,
    `getSMARTCCA_OPMODE`, `setACCESSORY_STATE`, `setBTCOEX_EXT_PROFILE`, `setDEVICE_ORIENTATION`,
    `setMITIGATE_INTERFERENCE`, `setOS_ELIGIBILITY`, `setTX_MODE_CONFIG` come in; `set6G_MODE`,
    `setWCL_LINK_STATE_UPDATE`, `getWIFI_BT_5G_POLICY`, `setWIFI_BT_5G_POLICY` go out. The WCL block is
    relocated +3/+10 depending on the subgroup.
  - `IO80211VirtualInterface` (+5): `flushFlowQueues`, `signalMulticastPackets`, the second overload
    of `p2pMulticastTx`, `syncDPSStats`, `setRxDataStallStats`/`setRxFlowSteering`; the pure
    `setMacAddress(ether_addr&)` moves from 415 to the end of its own block (488).
  - `IO80211AWDLProtocol`: same cells as 24H232 with a uniform +5 shift
    (`getSSID` 505 … `setAWDL_RTG_PEER_STATS_CONFIG` 567).
  - `IO80211Controller`: no changes (463 cells, same pure ones).

## 5. Slots of the methods the layer declares or overrides

Search by name and owner in the dumps of each build; the pure cells of
`IO80211InfraProtocol`/`IO80211VirtualInterface`/`IO80211AWDLProtocol` are named with the corresponding
Apple vtable of the same build. "Different signature" = the demangled name differs in some build
(detail in §6).

### Controller (RTLAirController over IO80211Controller; source vt_IO80211Controller and vt_AppleBCMWLANCore)

| method | 23J631 | 24H232 | 25G241 | status |
|---|---|---|---|---|
| `start` | 184 | 184 | 184 | SAME |
| `stop` | 185 | 185 | 185 | SAME |
| `free` | 18 | 18 | 18 | SAME |
| `getHardwareAddress` | 279 | 279 | 279 | SAME |
| `isCommandProhibited` | 394 | 394 | 394 | SAME |
| `getDRIVER_VERSION` | 413 | 408 | 408 | different slot |
| `getHARDWARE_VERSION` | 414 | 409 | 409 | different slot |
| `getCARD_CAPABILITIES` | 415 | 410 | 410 | different slot |
| `getPOWER` | 416 | 411 | 411 | different slot |
| `setPOWER` | 417 | 412 | 412 | different slot |
| `getCOUNTRY_CODE` | 418 | 413 | 413 | different slot |
| `getPLATFORM_CONFIG` | MISSING | 416 | 416 | MISSING in some build |
| `getLogger` | MISSING | 424 | 424 | MISSING in some build |
| `getFaultReporterFromDriver` | 436 | 432 | 432 | different slot |
| `createWorkQueue` | 395 | 395 | 395 | SAME |
| `getWorkQueue` | 396 | 397 | 397 | different slot |

### WCL (RTLAirInterface over IO80211InfraProtocol; source vt_IO80211InfraProtocol and Apple vtables)

| method | 23J631 | 24H232 | 25G241 | status |
|---|---|---|---|---|
| `enable` | 266 | 266 | 266 | SAME |
| `disable` | 267 | 267 | 267 | SAME |
| `setWCL_SCAN_REQ` | 659 | 592 | 601 | different slot |
| `setWCL_SCAN_ABORT` | 654 | 587 | 596 | different slot |
| `setWCL_ASSOCIATE` | 660 | 593 | 602 | different slot+signature |
| `setWCL_JOIN_ABORT` | 657 | 590 | 599 | different slot+signature |
| `setWCL_LEAVE_NETWORK` | 647 | 581 | 590 | different slot |
| `setWCL_LINK_UP_DONE` | 662 | 595 | 604 | different slot |
| `setWCL_REASSOC` | 648 | 582 | 591 | different slot |
| `setWCL_QOS_PARAMS` | MISSING | 594 | 603 | MISSING in some build |
| `setWCL_SET_SCAN_HOME_AWAY_TIME` | 663 | 596 | 605 | different slot |
| `setWCL_ROAM_PROFILE_CONFIG` | 650 | 585 | 594 | different slot |
| `setWCL_SET_ROAM_LOCK` | 649 | 583 | 592 | different slot |
| `setWCL_SOI_CONFIG` | MISSING | 640 | 648 | MISSING in some build |
| `setWCL_REAL_TIME_MODE` | 655 | 588 | 597 | different slot |
| `setWCL_UPDATE_FAST_LANE` | MISSING | 625 | 633 | MISSING in some build |
| `setROAM_PROFILE` | 605 | 552 | 562 | different slot+signature |
| `setBTCOEX_PROFILE` | 616 | 562 | 572 | different slot |
| `setBTCOEX_PROFILE_ACTIVE` | 617 | 563 | 573 | different slot |
| `setOS_FEATURE_FLAGS` | 672 | 604 | 612 | different slot |
| `setPOWERSAVE` | 569 | 538 | 548 | different slot |
| `setINFRA_ENUMERATED` | MISSING | 630 | 638 | MISSING in some build |
| `setIE` | 590 | 543 | 553 | different slot |
| `setBSS_BLACKLIST` | MISSING | 572 | 582 | MISSING in some build |
| `setUSB_HOST_NOTIFICATION` | 630 | 570 | 580 | different slot |
| `setVIRTUAL_IF_CREATE` | 593 | 546 | 556 | different slot |
| `getCHANNEL` | 453 | 465 | 469 | different slot |
| `getRSSI` | 462 | 470 | 474 | different slot |
| `getRATE` | 456 | 468 | 472 | different slot |
| `getOP_MODE` | 461 | 469 | 473 | different slot |
| `getNSS` | 526 | 504 | 508 | different slot |
| `getMCS` | 475 | 473 | 477 | different slot |
| `getMCS_VHT` | 491 | 476 | 480 | different slot |
| `getMAX_NSS_FOR_AP` | 512 | 495 | 499 | different slot |
| `getBEACON_INFO` | 522 | 502 | 506 | different slot |
| `getSUPPORTED_CHANNELS` | 464 | 471 | 475 | different slot |
| `getHW_SUPPORTED_CHANNELS` | 507 | 490 | 494 | different slot |
| `getCHANNELS_INFO` | 493 | 477 | 481 | different slot |
| `getCOUNTRY_CHANNELS_INFO` | 538 | 513 | 517 | different slot |
| `getROAM_PROFILE` | 496 | 479 | 483 | different slot+signature |
| `getWCL_BSS_INFO` | 550 | 520 | 524 | different slot |
| `getWCL_TRAFFIC_COUNTERS` | 551 | 521 | 525 | different slot |
| `getWCL_FW_HOT_CHANNELS` | 548 | 518 | 522 | different slot |
| `getWCL_LOW_LATENCY_INFO` | 549 | 519 | 523 | different slot |
| `getWCL_EXTENDED_BSS_INFO` | 560 | 528 | 531 | different slot |

### VirtualInterface, redeclared inherited cells (AWDLInterface.h)

| method | 23J631 | 24H232 | 25G241 | status |
|---|---|---|---|---|
| `configureReport` | 133 | 133 | 133 | SAME |
| `updateReport` | 134 | 134 | 134 | SAME |
| `terminate` | 192 | 192 | 192 | SAME |
| `newUserClient` | 238 | 238 | 238 | SAME |
| `stringFromReturn` | 240 | 240 | 240 | SAME |
| `prepareBSDInterface` | 283 | 285 | 285 | different slot |
| `processBSDCommand` | 288 | 290 | 290 | different slot |
| `setInterfaceEnable` | 291 | 293 | 294 | different slot |
| `getFeatureFlags` | 308 | 310 | 311 | different slot |
| `bpfTap` | 313 | 315 | 316 | different slot |
| `setMulticastAddresses` | 337 | 336 | 337 | different slot |
| `createPeerManager` | 353 | 352 | 353 | different slot |
| `postMessage` | 354 | 354 | 355 | different slot |
| `logTxPacket` | 358 | 358 | 359 | different slot |
| `logTxCompletionPacket` | 359 | 359 | 360 | different slot+signature |
| `inputPacket` | 361 | 361 | 362 | different slot+signature |
| `logSkywalkTxReqPacket` | 363 | 363 | 364 | different slot |
| `pendingPackets` | 364 | 364 | 365 | different slot |
| `packetSpace` | 365 | 365 | 366 | different slot |
| `linkState` | 369 | 369 | 369 | SAME |
| `setScanningState` | 370 | 370 | 370 | SAME |
| `updateLinkParameters` | 373 | 374 | 374 | different slot |
| `updateInterfaceCoexRiskPct` | 374 | 375 | 375 | different slot |
| `setInterfaceNF` | 380 | 381 | 381 | different slot |
| `setDebugFlags` | 383 | 384 | 384 | different slot |
| `debugFlags` | 384 | 385 | 385 | different slot |
| `setInterfaceChipCounters` | 385 | 386 | 386 | different slot |
| `setInterfaceMIBdot11` | 386 | 387 | 387 | different slot |
| `setFrameStats` | 387 | 388 | 388 | different slot |
| `setInfraSpecificFrameStats` | 388 | 389 | 389 | different slot |
| `getWmeTxCounters` | 389 | 390 | 391 | different slot |
| `setWoWEnabled` | 394 | 392 | 393 | different slot |
| `wowEnabled` | 395 | 393 | 394 | different slot |
| `printDataPath` | 396 | 394 | 395 | different slot |
| `findOrCreateFlowQueue` | 397 | 396 | 397 | different slot |
| `findOrCreateFlowQueueWithCache` | 398 | 397 | 398 | different slot |
| `findExistingFlowQueue` | 399 | 398 | 399 | different slot |
| `removePacketQueue` | 382 | 383 | 383 | different slot |
| `flushPacketQueues` | 401 | 400 | 401 | different slot |
| `cachePeer` | 402 | 401 | 402 | different slot |
| `shouldLog` | 403 | 402 | 403 | different slot |
| `vlogDebug` | 404 | 403 | 404 | different slot |
| `vlogDebugBPF` | 405 | 404 | 405 | different slot |
| `getP2PSkywalkPeerMgr` | 408 | 407 | 408 | different slot |
| `findPeer` | MISSING | 409 | 410 | MISSING in some build |
| `getEventPipeSize` | 440 | 439 | 439 | different slot |
| `createEventPipe` | 441 | 440 | 440 | different slot |

### VirtualInterface, own block (AWDLInterface.h; owner IO80211VirtualInterface)

| method | 23J631 | 24H232 | 25G241 | status |
|---|---|---|---|---|
| `init` | 446 | 460 | 461 | different slot |
| `setLinkState` | 447 | 461 | 462 | different slot |
| `forwardPacket` | 448 | 462 | 463 | different slot |
| `handleIoctl` | 449 | 463 | 464 | different slot |
| `addPeerToCache` | 450 | 464 | 465 | different slot |
| `deletePeerFromCache` | 451 | 465 | 466 | different slot |
| `handleDebugCmd` | 452 | 466 | 467 | different slot |
| `getPeerSlotDataStats` | MISSING | 467 | 468 | MISSING in some build |
| `getPeerDataStats` | MISSING | 468 | 469 | MISSING in some build |
| `clearPeerDataStats` | MISSING | 469 | 470 | MISSING in some build |
| `setAwdlCurrentChannelSequenceIndex` | MISSING | 470 | 471 | MISSING in some build |
| `isOutputFlowControlled` | 453 | 471 | 472 | different slot |
| `setOutputFlowControlled` | 454 | 472 | 473 | different slot |
| `clearOutputFlowControlled` | 455 | 473 | 474 | different slot |
| `flushFlowQueues` | MISSING | MISSING | 475 | MISSING in some build |
| `configureAQMOutput` | 456 | 474 | 476 | different slot |
| `attachToBpf` | 457 | 475 | 477 | different slot |
| `configureIfnet` | 458 | 476 | 478 | different slot |
| `reset` | 459 | 477 | 479 | different slot |
| `dupAndTransmitMcastPacket` | 460 | 478 | 480 | different slot |
| `freeMulticastPacket` | 461 | 479 | 481 | different slot |
| `triggerAWDLMulticastTx` | 462 | 480 | 482 | different slot |
| `p2pMulticastTx` | 463 | 481 | 483 | different slot |
| `signalMulticastPackets` | MISSING | MISSING | 485 | MISSING in some build |
| `sendPendingPacketsToStack` | 464 | 482 | 486 | different slot |
| `freePendingPackets` | 465 | 483 | 487 | different slot |

### IO80211AWDLProtocol, pure block (names by AppleBCMWLANProximityInterface)

| method | 23J631 | 24H232 | 25G241 | status |
|---|---|---|---|---|
| `setMacAddress` | MISSING | 415 | 488 | MISSING in some build |
| `getSSID` | 485 | 500 | 505 | different slot |
| `getCHANNEL` | 486 | 501 | 506 | different slot |
| `getBSSID` | 487 | 502 | 507 | different slot |
| `getSTATE` | 488 | 503 | 508 | different slot |
| `getAWDL_SYNC_PARAMS` | 489 | 504 | 509 | different slot |
| `getAWDL_SYNC_ENABLED` | 490 | 505 | 510 | different slot |
| `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 491 | 506 | 511 | different slot |
| `getAWDL_ELECTION_ID` | 492 | 507 | 512 | different slot |
| `getAWDL_ELECTION_METRIC` | 493 | 508 | 513 | different slot |
| `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 494 | 509 | 514 | different slot |
| `getPEER_CACHE_MAXIMUM_SIZE` | 495 | 510 | 515 | different slot |
| `getAWDL_MASTER_CHANNEL` | 496 | 511 | 516 | different slot |
| `getAWDL_SYNC_STATE` | 497 | 512 | 517 | different slot |
| `getAWDL_ELECTION_RSSI_THRESHOLDS` | 498 | 513 | 518 | different slot |
| `getAWDL_PRESENCE_MODE` | 499 | 514 | 519 | different slot |
| `getAWDL_ELECTION_PARAMS` | 500 | 515 | 520 | different slot |
| `getAWDL_DEVICE_CAPABILITIES` | 501 | 516 | 521 | different slot |
| `getAWDL_SECONDARY_MASTER_CHANNEL` | 502 | 517 | 522 | different slot |
| `getAWDL_AF_TX_MODE` | 503 | 518 | 523 | different slot |
| `getAWDL_MIN_RATE` | 504 | 519 | 524 | different slot |
| `getAWDL_ENCRYPTION_TYPE` | 505 | 520 | 525 | different slot |
| `getAWDL_TXCAL_PERIOD` | 506 | 521 | 526 | different slot |
| `getAWDL_DEV_STATS` | 507 | 522 | 527 | different slot |
| `getAWDL_DFSP_CONFIG` | 508 | 523 | 528 | different slot |
| `getAWDL_CCA` | 509 | 524 | 529 | different slot |
| `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | 510 | 525 | 530 | different slot |
| `getLLW_PARAMS` | 511 | 526 | 531 | different slot |
| `setSSID` | 512 | 527 | 532 | different slot |
| `setCHANNEL` | 513 | 528 | 533 | different slot |
| `setAWDL_SYNC_PARAMS` | 514 | 529 | 534 | different slot |
| `setAWDL_SYNC_ENABLED` | 515 | 530 | 535 | different slot |
| `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 516 | 531 | 536 | different slot |
| `setAWDL_ELECTION_ID` | 517 | 532 | 537 | different slot |
| `setAWDL_BSSID` | 518 | 533 | 538 | different slot |
| `setAWDL_ELECTION_METRIC` | 519 | 534 | 539 | different slot |
| `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 520 | 535 | 540 | different slot |
| `setAWDL_MASTER_CHANNEL` | 521 | 536 | 541 | different slot |
| `setAWDL_SYNC_STATE` | 522 | 537 | 542 | different slot |
| `setAWDL_ELECTION_RSSI_THRESHOLDS` | 523 | 538 | 543 | different slot |
| `setAWDL_PRESENCE_MODE` | 524 | 539 | 544 | different slot |
| `setPEER_CACHE_CONTROL` | 525 | 540 | 545 | different slot |
| `setAWDL_OOB_REQUEST` | 526 | 541 | 546 | different slot |
| `setAWDL_SYNC_FRAME_TEMPLATE` | 527 | 542 | 547 | different slot |
| `setAWDL_ELECTION_PARAMS` | 528 | 543 | 548 | different slot |
| `setAWDL_PEER_TRAFFIC_REGISTRATION` | 529 | 544 | 549 | different slot |
| `setAWDL_SECONDARY_MASTER_CHANNEL` | 530 | 545 | 550 | different slot |
| `setAWDL_AF_TX_MODE` | 531 | 546 | 551 | different slot |
| `setAWDL_MIN_RATE` | 532 | 547 | 552 | different slot |
| `setAWDL_ENCRYPTION_KEYS` | 533 | 548 | 553 | different slot |
| `setAWDL_ENCRYPTION_TYPE` | 534 | 549 | 554 | different slot |
| `setAWDL_OOB_AUTO_REQUEST` | 535 | 550 | 555 | different slot |
| `setAWDL_TXCAL_PERIOD` | 536 | 551 | 556 | different slot |
| `setAWDL_AWDL_ADVERTISERS` | 537 | 552 | 557 | different slot |
| `setAWDL_DFSP_CONFIG` | 538 | 553 | 558 | different slot |
| `setAWDL_DFSP_UCSA_CONFIG` | 539 | 554 | 559 | different slot |
| `setCOMPANION_SKYWALK_IF_CREATE` | 540 | 555 | 560 | different slot |
| `setAWDL_OOB_SEC_PAYLOAD_REQUEST` | 541 | 556 | 561 | different slot |
| `setLLW_PARAMS` | 542 | 557 | 562 | different slot |
| `setTX_RATE` | 543 | 558 | 563 | different slot |
| `setP2P_COEX_PROTECTION` | 544 | 559 | 564 | different slot |
| `setPSF_DWELL` | 545 | 560 | 565 | different slot |
| `setAWDL_CONFIG` | MISSING | 561 | 566 | MISSING in some build |
| `setAWDL_RTG_PEER_STATS_CONFIG` | 546 | 562 | 567 | different slot |

## 6. Detected signature changes

Read from the demangled names of the dumps (the parameter types are part of the
name); a parameter type change changes the slot even if the short name matches:

| method | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| `setWCL_ASSOCIATE` | `apple80211_assoc_candidates*` | `apple80211AssocCandidates*` | `apple80211AssocCandidates*` |
| `getROAM_PROFILE` / `setROAM_PROFILE` | `apple80211_roam_profile_band_data*` | `apple80211_roam_profile_all_bands*` | `apple80211_roam_profile_all_bands*` |
| `setWCL_JOIN_ABORT` | `void*` | `void*` | `apple80211_wcl_abort_join*` |
| `setLinkState` (chain cell, 368 → 461) | `(IO80211LinkState, unsigned, bool, unsigned)` | same as 23J631 | `(IO80211LinkState, unsigned, bool, unsigned, unsigned)` |
| `setMacAddress` (pure in the VI/AWDL chain) | does not exist | `ether_addr&` (slot 415) | `mloAddrArray&` (slot 468; the `ether_addr&` pure moves to 488) |

For the layer: in 23J631 the parameter of `setWCL_ASSOCIATE` is another type (same pointer, different
structure by name), and the roam profiles use `apple80211_roam_profile_band_data`; the
setter `setAWDL_CONFIG` does not exist and `setWCL_JOIN_ABORT` arrives typed as `void*`.

## 7. Implications for the port

- Per-build generation: slot-exact header, stubs and `KnownBuilds.inc` separate for 23J631 and 24H232,
  from the dumps of `Docs/re/io80211_23J631/` and `Docs/re/io80211_24H232/`. Reusing the binary
  of one in the other would misplace every virtual call of the WCL/Controller block (§5).
- In 23J631 the InfraProtocol stub set must cover the ~96 classic ioctls that Sequoia removed,
  and must not declare the six nonexistent methods (`getPLATFORM_CONFIG`, `getLogger` of the controller,
  the five new WCL setters and `setMacAddress(mloAddrArray&)`): a redeclaration would land in a slot
  that in Sonoma belongs to another method.
- In 23J631 the AWDL block has neither `setAWDL_CONFIG` nor a pure `setMacAddress(ether_addr&)`:
  `RTLAwdInterface` loses two overrides and the remaining 62 pure cells shift −15
  with respect to 24H232 (`getSSID` 485 versus 500).
- CoreCapture: `getLogger` does not exist as a controller virtual in 23J631 (it does in 24H232/25G241,
  pure at 424) and `getFaultReporterFromDriver` is concrete at 436 in Sonoma. The pipe startup that
  the layer does before `IO80211Controller::start` responded to the 25.6.0 panic; in Sonoma the
  starting requirement is different (?) and it is advisable to verify what its `start` requires before porting.
- Slots stable in the three builds (favorable signal, not sufficient): `start` (184), `stop` (185),
  `free` (18), `getHardwareAddress` (279), `isCommandProhibited` (394), `createWorkQueue` (395),
  `enable` (266) and `disable` (267) of the interface.
- RegistrationInfo and bring-up: this analysis covers vtables, sizes and constructors; the offsets of
  structs (`RegistrationInfo`, queues, pools) and the non-virtual functions of the 25.6.0 bring-up are
  not contrasted in 23J631/24H232 (?).

## 8. Not verified

- The 8 bytes of `IOSkywalkNetworkInterface` that separate the expansion pointer from the byte flag are
  not written in `init` (?): still to be identified in other functions, if they are used at all.
- The exact order of cells within the 128-281 span of the interface classes is identical between
  builds except for the events listed in §4; not every individual cell of that span has been traced (?).
- `IOService` and its span of the controller (cells with a symbol-less target in the dumps) is inherited
  from the SDK and takes no part in the port.
- The runtime behaviour (load, `start`, panics) of either build: only static evidence exists.

## 9. Log

- `Docs/re/io80211_23J631/` and `Docs/re/io80211_24H232/`: six symbol dumps
  (`syms.txt` = IO80211Family; `com.apple.kernel`, `IONetworkingFamily`, `IOSkywalkFamily`,
  `corecapture`, `syms_bcm.txt` = AppleBCMWLANCoreMac), `classes.txt` (class names derived from
  the vtable symbols), 15 `vt_*.txt` (chain of 9 classes, SDK parents, three Apple
  implementations and proximity) and `KC.md5`/`KC.sha256`.
- `Docs/re/io80211_25.6.0/`: added `vt_IO80211VirtualInterface.txt`,
  `vt_IO80211AWDLProtocol.txt`, `vt_AppleBCMWLANProximityInterface.txt`.
- Disassemblies of constructors and `init` cited in §3: copies of the analysis databases in
  their own working directory, not over the originals.
- sha256 of the KCs: 23J631 `3f5dcde57f2d0ce27b9c9abf3d1e3d2c36b33cea9ae0a7eb36bf9c4282ed8cd2`,
  24H232 `ddd9016e225a61ebfd4f04b02a00937262f26690c57a928613e08506978c81be`,
  25G241 `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc`.
