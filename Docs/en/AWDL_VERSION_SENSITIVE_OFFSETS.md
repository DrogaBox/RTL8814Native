# Version-sensitive offsets and structures of the AWDL stack (25G241 → macOS 14–26)

Single table for the per-version reanalysis of the kernelcache. It gathers from all the `Docs/AWDL_*.md` the
struct offsets and the vtable slots stage 1 depends on or that the predictions of the tests depend on
(T3/T3.5), with a byte pattern that locates each access in the owning
function to find it again — and measure it — in a kernelcache of another version. Complements
[`AWDL_COMPAT_DEPENDENCIES.md`](AWDL_COMPAT_DEPENDENCIES.md) (imported symbols and behavior
assumptions) and is the "per-build displacement table"
[`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md`](AWDL_STAGE1_OUTPUT_HANDLER_LINK.md) §7 anticipated. Base:
kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64); reads via IDA headless (`idat -A -S`)
over a copy of `boot_awdl.kc.i64` in `Build/ida_awdl/task_lifecycle/`.

## 0. Conventions

- **Location**: every function is looked up by symbol in the `LC_SYMTAB` of the fileset (never by VA; the
  VAs of this document change in every version). The pattern is searched **inside the owning function**.
- **Pattern**: hexadecimal bytes of the disassembly; `??` = one-byte wildcard (modrm/sib/register fields,
  which change if the compiler reassigns registers). **The displacement is literal**: if the pattern matches,
  the offset is still the same; if it does not match, the offset moved. Second level of the recipe (§9):
  wildcard the displacement, locate the access anyway and **measure** the new value.
- **✓** = bytes read in the 25G241 IDB (probes of `task_lifecycle`, `lc_out8`/`lc_out9`);
  **(d)** = pattern derived from the disassembly cited by the source document, without a byte read.
- **Risk**: what happens if the offset moves and the gate does not readjust it — *silence* (corruption or
  wrong behavior without error), *panic*, *lost feature* (visible failure), *analysis* (only
  invalidates test predictions; the driver does not consume it).
- The vtable offsets are relative to the vptr (`ZTV+X` = raw cell, `vptr+(X−0x10)`).

## 1. `RegistrationInfo` (struct filled by the driver; the family validates and copies it)

The driver writes this struct (`AWDLInterface.cpp:188-199`); the family validates it by version and size and
makes two copies (`iface+0x118` and `iface+0xC0`, `AWDL_STAGE1_PENDING_CHECKS.md` §1). If Apple moves a
field **without bumping the version**, the driver value lands in the wrong field of the copy: silent
failure. A version or minimum-size bump is detected by itself (registration rejection, `0xE00002C2`).

| Field | 25G241 | Family read (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x00` version = 1 | 1 | `registerNetworkInterface` `0xffffff8002b0a204` | (d) `cmp [reg],1` prior to the `+4` | registration gate | visible rejection of the registration |
| `+0x04` size ≥ `0x108` | 0x108 | same | ✓ `41 81 7C 24 04 08 01 00 00` (`0xffffff8002b0a23c`) | registration gate | visible rejection |
| struct size `0x130` | 0x130 | `copyRegistrationInfo` `0xffffff8002b10a84` (requires ≥ 0x130, `0xffffff8002b10a08`–`0x10a15`) | (d) no pattern | registration gate | visible rejection |
| `+0x3C` headroom (u16) | 64 | `getTxDataOffset` `0xffffff8002b0baba` | ✓ `0F B7 ?? 3C` (`0xffffff8002b0bad2`) | headroom of the BSD client → `ifp+0x58C` (`AWDL_STAGE1_LIFECYCLE_CHECKS.md` §1) | **silence**: headroom 0, packets without reserve |
| `+0x3E` (sibling u16) | 0 | `IOSkywalkNetworkBSDClient::start` `0xffffff8002aff562` | ✓ `0F B7 ?? 3E` (`0xffffff8002affaa3`) | second data offset → `ifp+0x58E` | silence (sibling field at 0) |
| `+0x48`/`+0x4C` MTU and max MTU (u32×2) | 0x594 | `AppleBCMWLANSkywalkInterface::start` `0xffffff80015d909a` (subtracts the headroom; additional reader `?`) | ✓ `41 29 ?? 48 41 29 ?? 4C` (`0xffffff80015d9593`, adjacent pair) | registered MTU | silence: MTU without subtracting the headroom |
| `+0x108` MAC (6 bytes) | — | `initBSDInterfaceParameters` `0xffffff8002b10bca` (memcmp and LLaddr) | ✓ `8B ?? 08 01 00 00` (`0xffffff8002b10c24`, `0xffffff8002b10c4f`) | BSD address | silence: empty or garbage MAC |
| `+0x38` unit (u32) | 0 | via slot `+0x9F0` (`0xffffff8002aff82b`) and property `IOInterfaceUnit`; internal offset `?` | (d) no pattern | BSD name/unit | visible: wrong "awdlN" name |
| `+0x40` flags (u32) | 0 | scheduler model of the `ifclassq` (`AWDL_STAGE1_TXPUSH_LLINK.md` §2); internal reader `?` | (d) no pattern | TX discipline | silence: different discipline |
| `+0x28` (u32 seeded by the family) | 0x20 | `initRegistrationInfo` (calls slot `0x9C0` at `0xffffff8002b10973`) | ✓ `41 FF 94 24 C0 09 00 00` (same slot `0x9C0`, SIB encoding) | field default | silence |

The `RegInfo` that `AppleBCMWLANSkywalkInterface::start` receives is mutated by the family (MTU − headroom,
§3 of `AWDL_STAGE1_LIFECYCLE_CHECKS.md`): a driver must not reuse it without filling it again.

## 2. `ifnet_init_eparams` (stack struct of the `start` of the BSD client)

Only the family and the kernel touch it; for the driver it matters twice: the header `{version, size}`
is the gate protecting all the fields (if Apple bumps it, the base returns `0xE00002C2` and the override
of `initBSDInterfaceParameters` of stage 2 does not run), and field `+0x40` is route A of the design of
stage 2 (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` §5.3).

| Field | 25G241 | Verification (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x00` version = 2 | 2 | `ifnet_allocate_extended` `0xffffff800063f3f0` (`0xffffff800063f432`) and `initBSDInterfaceParameters` (`0xffffff8002b10bcf`) | ✓ `83 3F 02` (alloc, over rdi) / ✓ `83 3E 02` (initBSD, over rsi) | gate of the whole struct | the override stops running (lost feature) |
| `+0x04` size = `0x158` | 0x158 | same (`0xffffff800063f440`: `>= 0x158`; initBSD `0xffffff8002b10be8`: `== 0x158`) | ✓ `81 7F 04 58 01 00 00` (alloc) / ✓ `81 7E 04 58 01 00 00` (initBSD) | gate | same |
| `+0x08` flags | 0x80000000 | alloc: `& 3 == 0` (pseudocode §1.3 of LIFECYCLE) | (d) no pattern | registration flags | visible rejection |
| `+0x38` `if_output` of the driver | `IOSkywalkNetworkBSDClient::if_output` | `start` `0xffffff8002aff562` (`0xffffff8002affa54`); **the registration replaces it** (`0xffffff800063f882`) | (d) no pattern | initial output | null: always replaced |
| `+0x40` optional `output` | 0 | `ifnet_allocate_extended`: `[ifp+0x260] = [params+0x40]` (`0xffffff800063f890`) | ✓ `49 89 85 60 02 00 00` (`0xffffff800063f890`) | route A of stage 2 | **silence**: the project's own output is not installed |
| `+0x48` (required 0) | 0 | alloc of the provider `0xffffff80009a4140` (`0xffffff80009a41f4`) | (d) no pattern | registration gate | visible rejection |
| `+0x144` tx data offset (u16) | RegInfo+0x3C | `start` writes at `0xffffff8002affa94` (displacement `+0x13C` over r13 = base+8); `ifnet_allocate_extended` reads at `0xffffff800063f626` | ✓ `66 41 89 ?? 3C 01 00 00` (bias +8) and ✓ `44 0F B7 ?? 44 01 00 00` (`0xffffff800063f626`) | headroom of TX | **silence**: `ifp+0x58C` at 0 |
| `+0x146` (sibling u16) | RegInfo+0x3E | same (`0xffffff800063f62e`) | ✓ `0F B7 ?? 46 01 00 00` (`0f b7 87 46 01 00 00`, no REX: destination is eax) | second offset | silence |
| `+0x148` (u32) | `[client+0x130]` or 300/0 | same (`0xffffff800063f639`) | (d) no pattern | third field of the trio | silence |

## 3. `ifnet` (BSD struct; the driver does not touch it in stage 1, stage 2 does)

For stage 1 these offsets only support T3 predictions; for stage 2 (writing of
`ifp+0x258`) they are direct risk of **silent corruption** of the `ifnet`.

| Field | 25G241 | Owning function (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x78` `if_refflags` (u32, `& 7 == 2`) | — | `ifnet_enqueue_pkt` `0xffffff80005e65d0` | ✓ `8B ?? 78 83 E0 07 83 F8 02` (`0xffffff80005e65f5`–`0xffffff80005e65fe`) | enqueuing gate (T3: without this the ND/RS does not enter) | **analysis/stage 2**: TX path does not start |
| `+0xBE` `if_flags` (u16) | IFF_UP | `ifnet_flags` `0xffffff8000640390` (u16 at `+190`, `AWDL_STAGE1_IFUP_PATH.md` §8); gate `test byte [rdi+0BEh], 1` (`0xffffff80005e6600`) | ✓ `F6 ?? BE 00 00 00 01` | `SIOCSIFFLAGS` → ifup | analysis |
| `+0xC0` `if_eflags` | IFEF_TXSTART = 0x80 | `ifnet_allocate_extended` `0xffffff800063f3f0` (`0xffffff800063fc0a`); gates of `sub_FFFFFF80005E6110` (`0xffffff80005e65ec`, `0xffffff80005e61b2`) | ✓ `F0 41 81 ?? C0 00 00 00 80 00 00 00` (write); ✓ `F6 ?? C0 00 00 00 80` (reads) | trigger of the TX startup thread | analysis/stage 2 |
| `+0xC3` (byte, `& 4` IFEF_SKYWALK_NATIVE) | — | thread `ifnet_start_%s` (test at `0xffffff80005ea8bd`; the citation `0xffffff80005ea8a6` of TXPUSH §8 points to the `lea` of the neighboring literal) | ✓ `F6 83 C3 00 00 00 04` | name of the doorbell | analysis |
| `+0x248` `if_output` (stack entry) | `_dlil_output_handler` | `ifnet_allocate_extended` (`0xffffff800063fc4f`–`0xffffff800063fc56`) | ✓ `49 89 8D 48 02 00 00` (store after the `lea`) | TX entry of the stack (`AWDL_STAGE1_OUTPUT_HANDLER_LINK.md` §3) | **silent corruption** if stage 2 writes it wrong |
| `+0x250` `if_start` | `_ifp_if_start` | same (`0xffffff800063f89d`) | ✓ `4D 89 BD 50 02 00 00` | purge of the startup thread | **panic** (call of a wrong pointer) if stage 2 corrupts it |
| `+0x258` `output_handler` | `_ifnet_enqueue` | `_dlil_output_handler` `0xffffff80005ef500` (`0xffffff80005ef504`, `jmp rax` at `+8`); `ifnet_set_output_handler` `0xffffff80005ef540` (`lock cmpxchg` at `0xffffff80005ef54d`) | ✓ `48 8B ?? 58 02 00 00` ; ✓ `F0 48 0F B1 ?? 58 02 00 00` | destination of all the stack TX; target of stage 2 | **silent corruption / panic** |
| `+0x260` `output` of the registration | 0 (⇒ `_ifnet_enqueue`) | `ifnet_allocate_extended` (`0xffffff800063f890`, `0xffffff800063fc16`) | ✓ `49 8B 85 60 02 00 00` | backup of `+0x258` | silence (route A) |
| `+0x598` default `if_start` | `_ifp_if_start` | same (`0xffffff800063fcf3`) | ✓ `4D 89 B5 98 05 00 00` | restoration | silence |
| `+0x5A0` default `output_handler` | `_ifnet_enqueue` | same (`0xffffff800063fda5`); anchor of the `cmpxchg` (`0xffffff80005ef544`) | ✓ `49 89 85 A0 05 00 00` ; ✓ `48 8B ?? A0 05 00 00` | comparator of the `cmpxchg` of stage 2 | silence: the CAS would always fail (lost feature) |
| `+0x300`/`+0x304`/`+0x309` (state of the startup thread) | — | `sub_FFFFFF80005E6110` `0xffffff80005e6110` (`0xffffff80005e61ce`–`0xffffff80005e61e8`) | ✓ `F6 83 00 03 00 00 01` ; ✓ `FF 83 04 03 00 00` ; ✓ `80 BB 09 03 00 00 00` | conditions the wake of the thread | analysis/stage 2 |
| `+0x330` pointer of the startup thread | — | `ifnet_attach` (`0xffffff80005e9632`, panic if null after attach); `assert_wait` (`0xffffff80005eaa0c`) | ✓ `48 83 BB 30 03 00 00 00` ; ✓ `4C 8D BB 30 03 00 00` | wake of the purge | analysis/stage 2 |
| `+0x338` `ifclassq` | — | `sub_FFFFFF80005E6110` (`0xffffff80005e614d`); `ifnet_allocate_extended` (`0xffffff800063fe0a`) | ✓ `48 8B B3 38 03 00 00` ; ✓ `49 8B 85 38 03 00 00` | TX discipline | analysis |
| `+0x58C`/`+0x58E`/`+0x590` (tx/rx data offset + u32) | 64/0/… | `ifnet_allocate_extended` (`0xffffff800063fd48`, `0xffffff800063fd54`, `0xffffff800063fd62`); reader `nx_netif_host_output` (`0xffffff80009ea590`, `0xffffff80009ea796`) | ✓ `66 41 89 ?? 8C 05 00 00` ; ✓ `66 41 89 ?? 8E 05 00 00` ; ✓ `41 89 85 90 05 00 00` | headroom the netif adds to the TX packets (`AWDL_STAGE1_LIFECYCLE_CHECKS.md` §1.4) | silence: packets without headroom (the field is filled from the RegInfo; moving it in the kernel only affects the analysis) |
| `+0x900` embedded `netem` queue | — | `sub_FFFFFF80005E6110` (`0xffffff80005e6133`) | ✓ `48 83 ?? 00 09 00 00 00` | diversion of the first packet | analysis (T3.5) |

## 4. `ifclassq` and discipline (pointed at by `ifp+0x338`)

| Field | 25G241 | Owning function (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x4C` bit 1 | purge flag | `sub_FFFFFF80005D5730` | ✓ `F6 43 4C 02` (`0xffffff80005d5782`) | purge of the startup thread (`AWDL_STAGE1_STAGE2_PUSH_DESIGN.md` §1) | analysis |
| `+0x68` table of the discipline | fq_codel | `sub_FFFFFF80005E6110` (`0xffffff80005e6158`) | ✓ `4C 8B ?? 68` | selects `enqueue` | analysis |
| table `+0x18` enqueue operation | `fq_if_enqueue` | same (`0xffffff80005e6172`) | ✓ `41 FF 52 18` | real enqueuing | analysis |
| `+0x5C` = 0x800 | limit | `ifnet_allocate_extended` (`0xffffff800063fe11`) | ✓ `C7 40 5C 00 08 00 00` | cap of the scheduler | analysis |

## 5. `netif_llink`: `nll_state` (`qset+0x3C` of the documents)

The byte `+0x3C` the documents called the "qset" one is `netif_llink::nll_state`
(`AWDL_STAGE1_QSET_STATE.md` §1/§2): 1 when creating the link, 2 when destroying it.

| Field | 25G241 | Owning function (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x3C` = 1 (creation) | 1 | `sub_FFFFFF80009F96E0` (link registration) | ✓ `41 C6 ?? 3C 01` (`0xffffff80009f98ce`) | live state | analysis |
| `+0x3C` = 2 (destruction) | 2 | `sub_FFFFFF80009F9C60` | ✓ `C6 ?? 3C 02` (`0xffffff80009f9c63`) | destroyed state | analysis |
| `+0x3C` == 2 ⇒ `ENXIO` | — | `kern_netif_queue_tx_dequeue` `0xffffff80009e6120` | ✓ `41 80 79 3C 02` (`0xffffff80009e614e`) | dequeue gate (T3.5) | analysis |
| `+0x3C` == 2 on enqueue | — | `netif_qset_enqueue` `0xffffff80009fa2a0` (`0xffffff80009fa2d3`, `AWDL_STAGE1_IFUP_PATH.md` §8) | (d) no bytes | second rejection | analysis |

## 6. Vtable slots cited by the documents

The complete range declared by the header of stage 1 (27 methods `0xE68`–`0xF38`, pure `setMacAddress`
`0xF40`, 16 reserved `0xF48`–`0xFC0`, 63 AWDL slots `0xFC8`–`0x11B8`) is rechecked by
`Tools/awdl_recheck_vtable.py` against the dumps of each version; here are only the slots with a
call cited in the documents and their access pattern. Encoding of an indirect call with a
32-bit displacement: `FF /2` (`FF 90+rm`, or `41 FF 94 24` with SIB if the base is r12–r15).

| Slot (vptr) | Method | Cited site (owning head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| `+0x9C0` | `IOSkywalkNetworkInterface::getTxDataOffset` | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002affa8e`) | ✓ `FF ?? C0 09 00 00` | headroom → eparams | **silence**: headroom 0 |
| `+0x9C0` (SIB variant) | same | `initRegistrationInfo` (`0xffffff8002b10973`) | ✓ `41 FF 94 24 C0 09 00 00` | seeds `RegInfo+0x28` | same |
| `+0x8E0` | `initBSDInterfaceParameters` | `start` of the client (`0xffffff8002affadc`) | ✓ `FF ?? E0 08 00 00` | route A of stage 2 (field `+0x40`) | lost feature |
| `+0x8E8` | `prepareBSDInterface` | `gatedPrepareNexus` `0xffffff8002b0066c` (`0xffffff8002b00688`) | ✓ `FF ?? E8 08 00 00` | route B of stage 2 (ifnet already created) | lost feature |
| `+0x600` | `terminate(uint)` | `destroySkywalkInterface` `0xffffff80016aae4e` (`0xffffff80016aaebc`, `esi=2`) | ✓ `FF ?? 00 06 00 00` | destruction of the reference | analysis |
| `+0x5B0` | `IOService::registerService(uint)` | `AppleBCMWLANProximityInterface::start` (`0xffffff800172982a`, `esi=2`) | ✓ `FF ?? B0 05 00 00` | birth of the `ifnet` (`AWDL_INTERFACE_BIRTH.md` §2) | no `ifnet`: lost feature |
| `+0x5D0` | `IOSkywalkBSDClient::start` | `start` of the client (`0xffffff8002aff5c8`) | ✓ `FF ?? D0 05 00 00` | startup of the BSD chain | analysis |
| `+0x5D8` | `IOSkywalkInterface::stop` (base) | `IOSkywalkNetworkInterface::stop` (`0xffffff8002b09e96`, `jmp`) | ✓ `FF ?? D8 05 00 00` | tail of the `stop` | analysis |
| `+0x1D8` | `OSOrderedSet::lastObject` | `deregisterNetworkInterface` (`0xffffff8002b09ec9`) | ✓ `FF ?? D8 01 00 00` | withdrawal of the link | analysis |
| `+0x1F8` | `OSOrderedSet::getObject` | `start` of the client (`0xffffff8002aff884`) | ✓ `FF ?? F8 01 00 00` | default link | analysis |
| `+0xE68` | `IO80211VirtualInterface::init` | super-calls (`AWDL_STAGE1_SMALL_CHECKS.md` §1.2; `0xffffff8001718e01`) | ✓ `FF ?? 78 0E 00 00` (cell ZTV+`0xE78`) | init of the virtual interface | panic (pure virtual) if the driver declared the cell wrong |
| `ZTV+0x14F0` | `AppleBCMWLANSkywalkInterface::start(core, RegInfo)` | `createSkywalkInterface` (`0xffffff80016aa874`, `0xffffff80016aac40`) | ✓ `FF ?? E0 14 00 00` | dispatch of the Broadcom `start` | analysis |

## 7. Ivars of the BSD client and of the interface cited

| Field | 25G241 | Owning function (head) | Pattern (✓/(d)) | Use | Risk |
|---|---|---|---|---|---|
| client `+0xE8` interface | — | `IOSkywalkNetworkBSDClient::start` (`0xffffff8002affa84`; created at `0xffffff8002aff69b`) | ✓ `48 8B ?? E8 00 00 00` | receiver of all the interface slots | analysis |
| client `+0xF0` copy of the RegInfo | — | same (`0xffffff8002aff74d`; load of `[iface+0xC0]` at `0xffffff8002aff73c`) | ✓ `48 89 ?? F0 00 00 00` | read of `RegInfo+0x3E` | analysis |
| client `+0xF8` `__ifnet *` | — | `gatedPrepareNexus` (`0xffffff8002b006a6`) | ✓ `4C 89 ?? F8 00 00 00` | the ifnet reaches the client | analysis |
| client `+0x13C` (dword, state of `setPowerStateHandler`) | 1 | `init`/`setPowerStateHandler`/`ioctlHandler` (`0xffffff8002aff551`, `0xffffff8002b00091`…) | ✓ `C7 83 3C 01 00 00 01 00 00 00` | wake flags | analysis (it is not the headroom: `LIFECYCLE_CHECKS` §1.1) |
| iface `+0xC0` / `+0x118` copies of the RegInfo | — | `registerNetworkInterface` (`0xffffff8002b0a292`) / `copyRegistrationInfo` (`0xffffff8002b10a26`) | (d) no pattern | source of all the fields of §1 | silence (if it moves, the copy is lost) |
| iface `+0xC8` `OSOrderedSet` of links | — | `registerNetworkInterface` (`0xffffff8002b0a39d`) | (d) no pattern | logical links | visible at registration |

## 8. Anchor functions (always by symbol; 25G241 VAs only as reference)

| Symbol | VA 25G241 | Role in the documents |
|---|---|---|
| `kern_nexus_controller_alloc_net_provider_instance` | `0xffffff80009a4140` | birth of the `ifnet`; requires `params+0x48 == 0` (`BSDSTART_ORDER` §2.5) |
| `nx_netif_validate_llink_config` | `0xffffff80009fad40` | validator of the registration (22 on `num_txqs == 0`, `QUEUE_SHAPE` §5) |
| `nx_netif_default_llink_config` | `0xffffff80009faf20` | copy of the `llink_init` |
| `kern_netif_queue_tx_dequeue` | `0xffffff80009e6120` | `nll_state`/ENXIO gate (T3.5) |
| `ifnet_allocate_extended` | `0xffffff800063f3f0` | writer of the fields of §3 |
| `ifnet_enqueue_pkt` | `0xffffff80005e65d0` | gate of `if_refflags` |
| `sub_FFFFFF80005E6110` (no symbol) | `0xffffff80005e6110` | `ifclassq` enqueuing + thread wake (TXPUSH §4) |
| `_dlil_output_handler` / `ifnet_set_output_handler` | `0xffffff80005ef500` / `0xffffff80005ef540` | `output_handler` chain (OUTPUT_HANDLER_LINK §3) |
| `IOSkywalkNetworkBSDClient::start` | `0xffffff8002aff562` | assembles the `ifnet_init_eparams` |
| `IOSkywalkNetworkInterface::getTxDataOffset` | `0xffffff8002b0baba` | reads `RegInfo+0x3C` |
| `IOSkywalkEthernetInterface::initBSDInterfaceParameters` | `0xffffff8002b10bca` | gate `{2, 0x158}` and MAC |
| `AppleBCMWLANCore::createSkywalkInterface` | `0xffffff80016aa874` | dispatch of the Broadcom `start` |
| `AppleBCMWLANSkywalkInterface::start` | `0xffffff80015d909a` | read of `RegInfo+0x3C` and MTU subtraction |

The 79 symbols stage 1 imports and the 14 explicit `__asm` are inventoried in
`AWDL_COMPAT_DEPENDENCIES.md` §1 and rechecked by `Tools/awdl_recheck_symbols.py`; this document does
not repeat them.

## 9. Consumption design by the recheck tools

**Exchange format.** The tables §1–§7 are dumped into a sibling data file (e.g.
`Tools/awdl_offsets_25G241.tsv`) with one row per access: `group; structure; field; offset; symbol of
the owning function; hex pattern (with `??`); displacement length (disp8/disp32); bias (0, or +8
for the eparams rows measured over r13)`. One file per reference build; the gate picks the
one of the running build (`VersionGate.h` already discriminates by `version_major`/`osversion`).

**`awdl_recheck_symbols.py`.** Today it walks the `LC_SYMTAB` of the filesets and checks the imported
symbols. Proposed extension: for each row of the file, resolve the owning symbol in the
target kernelcache (existing mechanism), bound the range of the function and search the pattern with
three verdicts: `OK` (the pattern with literal displacement matches ⇒ offset unchanged), `MOVED` (the
second-level pattern, with the displacement wildcarded, matches ⇒ the measured displacement and the new
value are reported for regenerating the row), `NOT-FOUND` (neither ⇒ manual re-read). No disassembler is
needed: the pattern is exact bytes with a per-byte wildcard (AND mask). The patterns with
bias (`eparams+0x144` measured as `+0x13C` over r13) are compared against `offset − bias`. Exit code
consistent with the existing one (0 without drift, 1 with drift, 2 usage).

**`awdl_recheck_vtable.py`.** Today it compares cell by cell the range declared in `AWDLInterface.h`
(`0x428`–`0xE60` verified; `0xE68`–`0x11B8` with the "MISMATCH = sibling branch" caveat already
documented). Proposed extension: (a) add the slots of §6 to the comparison against the dumps it
already produces (they are slots of the same vtables); (b) cross-corroboration with the other
script: if a slot moved in the dump, the pattern rows whose displacement encodes that
slot (`FF ?? C0 09 00 00` for `0x9C0`, etc.) must stop matching — the report can mark that row
as "explained by the slot drift" instead of "not found".

**`awdl_recheck_offsets.py`.** Implements this design over `Tools/awdl_offsets_25G241.tsv` (one row per access, pattern with `??`
over modrm/sib/register and literal displacement). Usage: `python3 Tools/awdl_recheck_offsets.py <BootKernelExtensions.kc> [--va-hint]`.
Without `--va-hint` the rows of functions without a symbol are omitted (`SKIPPED`); with it they are searched at the 25G241 VA and only count against that
build. Against 25G241 the result is 61 rows, 61 `OK` with `--va-hint` (48 `OK` and 13 `SKIPPED` without it); exit 0 without drift, 1 with drift, 2 usage.

**Risk→action correspondence.** `MOVED` on a *silence*-risk row of §1–§2 (RegInfo and
eparams) blocks the gate for that build; `MOVED` on rows of §3–§5 (ifnet/ifclassq/netif_llink) only
forces regenerating the T3 predictions; `NOT-FOUND` always asks for a manual re-read in the IDB.

## 10. Patterns verified byte by byte (25G241)

Read with `get_bytes` in the IDB (probes `lc_out8`/`lc_out9` of `task_lifecycle`), covering the
highest-risk rows (all the "silence" and "panic" risk ones of §1–§3): `RegInfo+0x3C`/`+0x3E`
(`0f b7 40 3c`, `0f b7 40 3e`), `RegInfo+0x48/0x4C` (`41 29 47 48 41 29 47 4c`), `RegInfo+0x108`
(`8b 80 08 01 00 00`, `8b 89 08 01 00 00`), `RegInfo+4` (`41 81 7c 24 04 08 01 00 00`),
`eparams+0x144` (`66 41 89 85 3c 01 00 00` in the writer, `44 0f b7 bf 44 01 00 00` in the reader of
`ifnet_allocate_extended`, `0f b7 87 46 01 00 00` the read of `+0x146`), header `{2, 0x158}` (`83 3e 02`, `81 7e 04 58 01 00 00`,
`83 3f 02`, `81 7f 04 58 01 00 00`), `ifp+0x58C/0x58E/0x590` (`66 41 89 85 8c 05 00 00`,
`66 41 89 85 8e 05 00 00`, `41 89 85 90 05 00 00`), `ifp+0x78` (`8b 47 78 83 e0 07 83 f8 02`),
`ifp+0xBE` (`f6 87 be 00 00 00 01`), `ifp+0xC0` (`f6 87 c0 00 00 00 80`, `f0 41 81 8d c0 00 00 00 80 00 00 00`),
`ifp+0x248` (`49 89 8d 48 02 00 00`), `+0x250` (`4d 89 bd 50 02 00 00`), `+0x258` (`48 8b 87 58 02 00 00`,
`f0 48 0f b1 b7 58 02 00 00`), `+0x260` (`49 89 85 60 02 00 00`), `+0x598` (`4d 89 b5 98 05 00 00`),
`+0x5A0` (`49 89 85 a0 05 00 00`, `48 8b 87 a0 05 00 00`), `+0x300/0x304/0x309/0x330`
(`f6 83 00 03 00 00 01`, `ff 83 04 03 00 00`, `80 bb 09 03 00 00 00`, `48 83 bb 30 03 00 00 00`),
`ifp+0x338` (`48 8b b3 38 03 00 00`, `49 8b 85 38 03 00 00`), `ifp+0x900` (`48 83 bf 00 09 00 00 00`),
`ifclassq+0x4C` (`f6 43 4c 02`), `+0x68` (`4c 8b 56 68`), table `+0x18` (`41 ff 52 18`),
`+0x5C` (`c7 40 5c 00 08 00 00`), `nll_state` (`41 c6 46 3c 01`, `c6 42 3c 02`, `41 80 79 3c 02`),
slots `0x9C0` (`ff 90 c0 09 00 00`, `41 ff 94 24 c0 09 00 00`), `0x8E0` (`ff 90 e0 08 00 00`),
`0x8E8` (`ff 90 e8 08 00 00`), `0x600` (`ff 90 00 06 00 00`), `0x5B0` (`ff 90 b0 05 00 00`),
`0x5D0` (`ff 90 d0 05 00 00`), `0x5D8` (`ff a0 d8 05 00 00`), `0x1D8` (`ff 90 d8 01 00 00`),
`0x1F8` (`ff 90 f8 01 00 00`), `0xE68` (`ff 90 78 0e 00 00`), `0x14E0` (`ff 90 e0 14 00 00`),
client `+0xE8`/`+0xF0`/`+0xF8` (`48 8b bb e8 00 00 00`,
`48 89 83 f0 00 00 00`, `4c 89 bb f8 00 00 00`), `+0x13C` (`c7 83 3c 01 00 00 01 00 00 00`),
`ifp+0xC3` (`f6 83 c3 00 00 00 04`). The same 61 sequences were re-verified by
`Tools/awdl_recheck_offsets.py` reading the kernelcollection file (probes `lc_out10` for the
last four).

## 11. What remains unverified

- The patterns marked **(d)**: `RegInfo+0x00` (version), size `0x130` of `copyRegistrationInfo`,
  `eparams+0x08`/`+0x38`/`+0x48`, `netif_qset_enqueue` `cmp [qset+3Ch], 2`
  (`0xffffff80009fa2d3`, cited by `IFUP_PATH`) and the ivars of §7 without
  a pattern. All of them have a VA cited by the source document and are verified with the same recipe.
- The family reader for `RegInfo+0x48/0x4C` outside the Broadcom `start` and for `RegInfo+0x40`
  (`?` inherited from `TXPUSH_LLINK` §2): the offsets are known, the consumer is not closed.
- The internal offset of `RegInfo+0x38` (unit) within the family: it reaches the kernel via slot
  `+0x9F0`, not through a citable direct access.
- Everything decided by the KPI tables and the kext loader (real symbol export per version) belongs to
  `AWDL_COMPAT_DEPENDENCIES.md` and is verified over the collection, not over the IDB.
- On earlier versions (Sonoma–15) no pattern has run yet: the "25G241 value" column is the
  only measurement; the first pass of the script fills the columns of the other versions.
