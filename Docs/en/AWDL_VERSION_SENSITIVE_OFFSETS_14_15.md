# Version-sensitive offsets of the AWDL stack: macOS 14.8.9 and 15.8.2 (23J631 / 24H232)

Port of the table of 25G241 ([`AWDL_VERSION_SENSITIVE_OFFSETS.md`](AWDL_VERSION_SENSITIVE_OFFSETS.md))
to the kernelcaches of macOS 14.8.9 (23J631, Darwin 23.6.0) and macOS 15.8.2 (24H232, Darwin 24.6.0),
both x86_64. Every row of the source tables (§1–§7 of the base document) was re-read in the corresponding kernelcache: the same access was located by the symbol of the owning function (or, for functions
without a symbol, by the equivalent function delimited by the same neighbors), the real offset or
slot was read and a byte pattern matching it was derived. The exchange data are in
[`Tools/awdl_offsets_23J631.tsv`](../../Tools/awdl_offsets_23J631.tsv) and
[`Tools/awdl_offsets_24H232.tsv`](../../Tools/awdl_offsets_24H232.tsv), with the same format and row
order as `Tools/awdl_offsets_25G241.tsv`.

Mechanical verification with `Tools/awdl_recheck_offsets.py --tsv --va-hint` against each kernelcache:

| Build | Rows | OK | Missing explained |
|---|---|---|---|
| 23J631 | 61 | 58 | 2 `NOT-FOUND` + 1 `MOVED` of noise over a missing row (§6) |
| 24H232 | 61 | 60 | 1 `NOT-FOUND` (§6) |

The `--va-hint` uses the VA column of each TSV, which already points at sites of the build itself: the files
are self-contained. Reads in IDA headless (`idat -A -S`) over copies of `boot_23J631.kc.i64` and of
an IDB rebuilt from `boot_24H232.kc` in a working directory of its own.

## 1. Conventions

- `=` = CONFIRMED (same value and equivalent pattern); `C` = CHANGED (new measured value); `MISSING`
  = the access no longer exists in the owning function (reason in §6).
- In the vtable rows, the value is the slot relative to the vptr; in the super-calls loading the
  ZTV directly (`stop`, virtual `init`) the pattern encodes the ZTV cell (`slot + 0x10`). The
  table of 25G241 already mixed both conventions in those two rows; it is kept as-is.
- The cited VAs are instruction heads read in the IDBs of each build; they change between versions
  and are only given as testimony of the finding.

## 2. Comparison table

### §1 `RegistrationInfo`

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x04` size ≥ `0x108` (registration gate) | 0x108 | `=` 0x108; owner renamed to `registerNetworkInterfaceWithLogicalLink` and encoding `81 7E 04` (C of symbol/encoding) | `=` (owner and encoding of 25G241) |
| `+0x3C` headroom (`getTxDataOffset`) | 0x3c | `=` | `=` |
| `+0x3E` (read of the client `start`) | 0x3e | `=` | `=` |
| `+0x48`/`+0x4C` MTU pair (Broadcom subtraction) | 0x48/0x4C | `=` | `=` |
| `+0x108` MAC | 0x108 | `=` | `=` |

### §2 `ifnet_init_eparams`

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x00` version == 2 (initBSD / alloc) | 2 | `=` | `=` |
| `+0x04` size `0x158` (initBSD ==, alloc ≥) | 0x158 | `=` | `=` |
| `+0x144` tx data offset (write of the `start`) | 0x144 | `=` 0x144; new SIB encoding (C of encoding) | `=` |
| `+0x144` tx data offset (read of the alloc) | 0x144 | `=` 0x144; no REX (C of encoding) | `=` |
| `+0x146` (read of the alloc) | 0x146 | `=` | `=` |

The struct `ifnet_init_eparams` (version 2, size 0x158) is intact in the three versions: the gate
protecting the stage 2 override does not move.

### §3 `ifnet`

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x78` `if_refflags` (& 7 == 2) | 0x78 | `=` | `=` |
| `+0xBE` `if_flags` (IFF_UP) | 0xbe | `=` | `=` |
| `+0xC0` IFEF_TXSTART (gates) | 0xc0 | `=` | `=` |
| `+0xC0` IFEF_TXSTART (write at registration) | 0xc0 | `=` 0xc0; SIB encoding (C of encoding) | `=` |
| `+0xC3` IFEF_SKYWALK_NATIVE | 0xc3 | `=` (same startup thread without a symbol) | `=` |
| `+0x248` `if_output` = `_dlil_output_handler` | 0x248 | C → **0x1C0** | C → **0x228** |
| `+0x250` `if_start` = `_ifp_if_start` | 0x250 | C → **0x1C8** | C → **0x230** |
| `+0x258` `output_handler` (jump of the stack) | 0x258 | C → **0x1D0** | C → **0x238** |
| `+0x258` `output_handler` (cmpxchg of installation) | 0x258 | C → **0x1D0** | C → **0x238** |
| `+0x260` `output` of the registration (write) | 0x260 | C → **0x1D8** | C → **0x240** |
| `+0x260` `output` of the registration (backup read) | 0x260 | C → **0x1D8** | C → **0x240** |
| `+0x598` default `if_start` | 0x598 | MISSING (store merged with that of `+0x250`, a single store to 0x1C8) | C → **0x578** |
| `+0x5A0` default `output_handler` (write and cmpxchg anchor) | 0x5a0 | C → **0x510** | C → **0x580** |
| `+0x300` bit 1 (thread running) | 0x300 | C → **0x270** | C → **0x2E0** |
| `+0x304` startup counter | 0x304 | C → **0x274** | C → **0x2E4** |
| `+0x309` gate byte | 0x309 | C → **0x279** | C → **0x2E9** |
| `+0x330` pointer of the startup thread (attach and `assert_wait`) | 0x330 | C → **0x2A0** | C → **0x310** |
| `+0x338` `ifclassq` (helper and registration) | 0x338 | C → **0x2A8** | C → **0x318** |
| `+0x58C`/`+0x58E`/`+0x590` tx/rx data offset + u32 | 0x58c/0x58e/0x590 | C → **0x4FC/0x4FE/0x500** | C → **0x56C/0x56E/0x570** |
| `+0x900` embedded netem queue | 0x900 | MISSING (outside the helper; successor ?) | MISSING (same ?) |

The fields stage 1/T3 only predict (gates `+0x78`, `+0xBE`, `+0xC0`, `+0xC3`) do not move;
the whole output block and startup-thread state is reorganized in each version.

### §4 `ifclassq` and discipline

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x4C` purge bit 1 | 0x4c | `=` (owner now with a symbol: `_if_qflush`) | `=` |
| `+0x68` table of the discipline | 0x68 | C of owner: the load lives in `_fq_if_enqueue_classq` (0x68 intact) | C of owner (same) |
| table `+0x18` enqueue operation | `call [table+0x18]` | C: direct `E8` call to `_fq_if_enqueue_classq` from the helper | C (same) |
| `+0x5C` limit 0x800 | 0x5c | `=` | `=` |

The dispatch by discipline table disappears from the enqueuing helper: the kernel calls
`_fq_if_enqueue_classq` directly and the indirection moved inside (23J631 `+0x4d`
`0xffffff800078edbd`; 24H232 `+0x4f` `0xffffff80007688ef`, both `mov rsi, [rdi+68h]`).

### §5 `netif_llink`

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0x3C` `nll_state` = 1 / = 2 | 0x3c | `=` (real owner: `_nx_netif_llink_add`; the patterns match intact) | `=` |
| `+0x3C` == 2 ⇒ ENXIO (dequeue) | 0x3c | `=` 0x3C; encoding `80 /5` (C of encoding) | `=` (same) |

### §6 Cited vtable slots

| Slot (vptr) | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `IOSkywalkNetworkInterface::getTxDataOffset` | 0x9C0 | C → **0x9A8** | C → **0x9B8** |
| `IOSkywalkEthernetInterface::initBSDInterfaceParameters` | 0x8E0 | C → **0x8D0** | `=` 0x8E0 |
| `IOSkywalkNetworkInterface::prepareBSDInterface` | 0x8E8 | C → **0x8D8** | `=` 0x8E8 |
| `IOService::terminate(uint)` | 0x600 | `=` | `=` |
| `IOService::registerService(uint)` | 0x5B0 | `=` | `=` |
| `IOSkywalkBSDClient::start` | 0x5D0 | `=` | `=` |
| `IOSkywalkInterface::stop` (super via ZTV `+0x5D8`) | 0x5D8 | `=` | `=` |
| `OSOrderedSet::lastObject` | 0x1D8 | C: renamed to **`getLastObject` at 0x1E0** and the call moved out of `deregisterNetworkInterface` into `IOSkywalkLogicalLink::setInterface` | `=` 0x1D8 (and still in `deregisterNetworkInterface`) |
| `OSOrderedSet::getObject` (in the client `start`) | 0x1F8 | MISSING from `start` (the slot exists; the default link is obtained by another way ?) | `=` 0x1F8 |
| `IO80211VirtualInterface::init` (super via ZTV `+0xE78`) | 0xE68 | C → **0xDF0** (ZTV cell `+0xE00`) | C → **0xE60** (ZTV cell `+0xE70`) |
| `AppleBCMWLANSkywalkInterface::start(core, RegInfo)` | 0x14E0 | C: devirtualized dispatch, `createSkywalkInterface` calls it directly (`E8` at `0xffffff80015ff8de`) | C → **0x1478** (still virtual) |

The slots `0x8E0`/`0x8E8` show non-monotonic drift: 23J631 moves them 0x10 down and 24H232
returns them to the value of 25G241. No slot can be deduced from the adjacent version.

### §7 Ivars of the BSD client

| Row | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `+0xE8` interface | 0xe8 | `=` | `=` |
| `+0xF0` copy of the RegInfo | 0xf0 | `=` | `=` |
| `+0xF8` pointer of the `ifnet` | 0xf8 | `=` | `=` |
| `+0x13C` wake state (`init` writes 1) | 0x13c | C → **`+0x120`**: the `init` zeroes the qword (`mov qword [rbx+120h], 0` at `0xffffff800285d192`) | C → **`+0x120`** (same, `0xffffff80029797b4`) |

## 3. Displacement map of the `ifnet` (25G241 → 23J631 → 24H232)

| Field | 25G241 | 23J631 | 24H232 |
|---|---|---|---|
| `if_output` (stack entry) | 0x248 | 0x1C0 | 0x228 |
| `if_start` | 0x250 | 0x1C8 | 0x230 |
| default `if_start` | 0x598 | (merged into `if_start`) | 0x578 |
| `output_handler` (destination of the stack TX) | 0x258 | 0x1D0 | 0x238 |
| `output` of the registration (`eparams+0x40`) | 0x260 | 0x1D8 | 0x240 |
| default `output_handler` (CAS anchor) | 0x5A0 | 0x510 | 0x580 |
| tx data offset / sibling / u32 | 0x58C / 0x58E / 0x590 | 0x4FC / 0x4FE / 0x500 | 0x56C / 0x56E / 0x570 |
| startup thread state (bit/counter/gate) | 0x300 / 0x304 / 0x309 | 0x270 / 0x274 / 0x279 | 0x2E0 / 0x2E4 / 0x2E9 |
| pointer of the startup thread (wait channel) | 0x330 | 0x2A0 | 0x310 |
| `ifclassq` | 0x338 | 0x2A8 | 0x318 |
| embedded netem queue | 0x900 | ? | ? |

## 4. What it means for stage 2 (output hook of AirLayer)

**The mechanism of route A is still alive.** The gate `{version 2, size 0x158}` of
`ifnet_init_eparams` and the field `params+0x40` do not change on 23J631 nor on 24H232 (§2), and the validations
of the `RegistrationInfo` (minimum size 0x108, headroom `+0x3C`, MTU `+0x48/0x4C`, MAC `+0x108`)
either (§1): the registration with headroom 64 and the delivery of the project's own `output` via `initBSDInterfaceParameters`
are still accepted by the family in the three versions. On 23J631 the function carrying the
`0x108` gate was renamed to `registerNetworkInterfaceWithLogicalLink`; the gate operates the same.

**The cells the family dispatches moved and not linearly.** The override of
`initBSDInterfaceParameters` that stage 2 installs on the Skywalk interface of the layer only receives the
call if the cell of the vtable of the class matches the slot that `IOSkywalkNetworkBSDClient::start`
invokes: 0x8E0 on 25G241, **0x8D0 on 23J631**, 0x8E0 on 24H232. The same applies to `prepareBSDInterface`
(route B): 0x8E8 → **0x8D8** → 0x8E8. A binary compiled with fixed cells of 25G241 loses the override
on 23J631 (silent lost feature: the call lands in the base implementation). The per-build gate
must select these two cells before any other.

**The slot every packet runs through was resettled twice.** `ifp+0x258` (`output_handler`, the
`jmp` of `_dlil_output_handler`) moves to `+0x1D0` on 23J631 and `+0x238` on 24H232; its
comparison anchor for the installation CAS (`ifnet_set_output_handler` and the default handler
stage 2 compares) moves from `+0x5A0` to `+0x510` and `+0x580`. Any write or direct comparison
over the `ifnet` with fixed offsets of 25G241 lands in a neighboring field: silent corruption or panic
(`if_start` is called by the purge thread). The stage 2 code must not carry these offsets
embedded; it must read them from the table of the running build.

**The T3/T3.5 predictions are regenerated per build.** The headroom trio the netif adds to the
packets (`+0x58C/0x58E/0x590`) lives at `+0x4FC/0x4FE/0x500` (23J631) and `+0x56C/0x56E/0x570` (24H232);
the linked `ifclassq` moves from `+0x338` to `+0x2A8` and `+0x318`; the startup thread state
(`+0x300/0x304/0x309`) to `+0x270/0x274/0x279` and `+0x2E0/0x2E4/0x2E9`; the thread pointer (wait
channel of the `assert_wait`) from `+0x330` to `+0x2A0` and `+0x310`. The netem queue `+0x900` disappears from
the enqueuing helper in both versions and its successor was not located (?): the T3.5 prediction of the
diversion of the first packet is left without an anchor on 14/15.

**Super-call of the init of the AWDL interface.** The ZTV cell the `init` of the virtual interface uses
for the super-call (pattern of the 14 `__asm` cells of stage 1) moves from `ZTV+0xE78`
(slot 0xE68) to `ZTV+0xE00` (0xDF0) on 23J631 and `ZTV+0xE70` (0xE60) on 24H232. The cell table of
`Tools/awdl_recheck_vtable.py` must be extended with these three positions.

**Discarded noise.** The plausible "MOVED" values of the first mechanical pass that the read in
IDA refuted: 0x230 in `_ifnet_attach` of 23J631 (it was a `cmp` of a neighboring pointer; the real thread is
at 0x2A0), 0x998/0x9a8 of the SIBs of `initRegistrationInfo` (first indirect call of the function,
not `getTxDataOffset`), 0xff30bd89/0xb70f44a2/0x4fc24/0xba/0x3a884/0xd40 (first matches of the
second pass with the displacement wildcarded), and 0x1d0/0x238 of the `+0x260`/`+0x598` rows (the
stores correspond to other fields of the resettled block).

## 5. Symbol and shape changes between builds

- `registerNetworkInterface` (4 arguments + flags) exists with that name on 25G241 and 24H232; on
  23J631 the pair of overloads is renamed (`registerNetworkInterface` with `IOSkywalkPacketQueue**` and
  `registerNetworkInterfaceWithLogicalLink`). Resolution by name must accept both.
- `OSOrderedSet::lastObject` → `getLastObject` on 23J631 (slot 0x1D8 → 0x1E0); 24H232 goes back to the
  previous name and slot. The call that on 25G241/24H232 is in
  `IOSkywalkNetworkInterface::deregisterNetworkInterface` lives on 23J631 inside
  `IOSkywalkLogicalLink::setInterface`.
- The enqueuing/wake helper (no symbol, between `_ifnet_enqueue` and `_ifnet_enqueue_pkt`) calls
  `_fq_if_enqueue_classq` directly on 23J631/24H232; the discipline table is consulted inside
  that function. The helper rows of the 25G241 table (`sub_FFFFFF80005E6110`) point in the new TSVs to the
  equivalents `sub_FFFFFF8000685460` (23J631) and `sub_FFFFFF80006470D0` (24H232), and the
  startup thread to `sub_FFFFFF800068B140` / `sub_FFFFFF800064B200`.
- `IOSkywalkNetworkBSDClient::init` no longer writes the wake dword to 1 at `+0x13C`: it zeroes the
  qword `+0x120` (23J631 and 24H232). The writers with a symbol (`setPowerStateHandler`,
  `ioctlHandler`) do not exist as such on 23J631 (?).
- `_if_qflush` and `_nx_netif_llink_add` — functions the 25G241 table treated as `sub_` — have
  a symbol in the symtab of the three versions; the new TSVs use them as owners.

## 6. Missing rows and open points

| Row | Builds | Status |
|---|---|---|
| `ifnet+0x598` default `if_start` | 23J631 | The store merged with that of `+0x250` into 0x1C8; on 24H232 it still exists (`+0x578`) |
| `ifnet+0x900` netem queue | 23J631 and 24H232 | Outside the enqueuing helper; successor not located (?) |
| `OSOrderedSet::getObject` from the client `start` | 23J631 | The `start` no longer walks the link set; slot 0x1F8 exists and `deregisterNetworkInterface` of 24H232 does call `lastObject` (0x1D8, `0xffffff8002983b74`) and `getObject` (0x1F8, `0xffffff8002983bc6`) |
| Writers of the wake ivar of the BSD client | 23J631 | `init` zeroes `+0x120`; the equivalent of `setPowerStateHandler` does not appear under that name (?) |

## 7. Consumption by the tools

The gate picks the TSV of the running build (the final column of each file carries the VA of the site in that
build, so `--va-hint` works against its own kernelcache). Regenerating a row after a
`MOVED` failure: wildcard the displacement, measure in the IDB of the build, rewrite pattern and offset, and
repeat `python3 Tools/awdl_recheck_offsets.py <kc> --tsv <tsv> --va-hint` until `OK` or until
classifying the row as MISSING in §6. The MISSING rows keep the pattern of 25G241 and remain as
expected `NOT-FOUND`/`MOVED`; the field column marks them with `[MISSING …]`.
