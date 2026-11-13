# Virtual interface role table in the kernel (macOS 26 / BootKernelExtensions)

Who reads/writes the role string tables seen at `0xffffff800246bda0` and `0xffffff800246be00`, the role↔name mapping, and the full chain that makes the AWDL interface end up with the property `IO80211VirtualInterfaceRole = AirLink`. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`, module base `0xffffff8000200000`), attached rea/IDA + file reads. Anything not seen goes as `?`.

## 1) The real tables and who uses them

**The addresses `0x246bda0` and `0x246be00` are internal cuts of two larger tables**, whose bases are the literals the code uses (that is why there are no xrefs to `0x246bda0` nor to `0x246be00`):

- **Name table**: base `0xffffff800246bd88`, 10 entries × 8 B (0x246bd88–0x246bdd7), indexed as `[role - 1]`.
- **Key table**: base `0xffffff800246bdd8`, 10 entries × 8 B (0x246bdd8–0x246be27), indexed as `[role - 1]`. The table of 5 seen at `0x246be00` are the entries of roles 6–10; those of roles 1–5 are at `0x246bdd8–0x246bdf8`.

**The reader/writer is `IO80211SkywalkInterface::setInterfaceRole(unsigned int)`** (`__ZN23IO80211SkywalkInterface16setInterfaceRoleEj`, **`0xffffff80022f60f0`**, 0x94 bytes). Observed pseudocode (addresses of each branch):

- `0xffffff80022f6101`: saves the raw role at `ivars + 88` (`*(*(this+36) + 88) = role`).
- `0xffffff80022f6104–0xffffff80022f6109`: `v3 = role - 1`; if `v3 > 9` (role 0 or ≥ 11) it uses error literals: key `"IO80211InterfaceRole"`, value `"Uknown"` (`0xffffff80022f6125`, `0xffffff80022f612c`).
- `0xffffff80022f6114`: **`name = off_FFFFFF800246BD88[role-1]`** (the name table).
- `0xffffff80022f611f`: **`key = off_FFFFFF800246BDD8[role-1]`** (the key table).
- `0xffffff80022f6139`: **`setPropertyHelper(this, key, name)`** — the property write.
- `0xffffff80022f616c–0xffffff80022f617f`: caches `OSString::withCString(name)` at `ivars + 184` (that is what `getInterfaceRoleStr`, `0xffffff80022f5ee8`, returns, delegating by reading that ivar).

The neighboring function `IO80211SkywalkInterface::getInterfaceRoleCounter(SkywalkInterfaceRole)` (**`0xffffff80022f5eb6`**, 0x22 bytes) confirms the range of the internal enum: `if (role <= 10) return intfRoleToNumberMap[role]++;` — a global counter map per role (map address: symbol `intfRoleToNumberMap`, exact VA not extracted, `?`).

## 2) Full name table (verified from the file and the IDB)

File of the `.kc`: the qwords are chained fixups; decoded with `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` (base = minimum vmaddr `__HIB`). Cross validation: entry `0x246bdb0` decodes to `0xffffff800241dbc0` = the exact string `AirLink`, and `0x246be00` → `0xffffff800241dbc8` = the exact string `IO80211VirtualInterfaceRole`.

| role (`SkywalkInterfaceRole`) | names entry | string | string VA | Source |
|---:|---|---|---|---|
| 1 | `0x246bd88` | `Infrastructure` | `0xffffff800241db9c` | file |
| 2 | `0x246bd90` | `LowLatency` | `0xffffff800241dc2a` | file |
| 3 | `0x246bd98` | `Uknown` | `0xffffff800241dc35` | file |
| 4 | `0x246bda0` | `Uknown` | `0xffffff800241dc35` | file + IDB |
| 5 | `0x246bda8` | `Uknown` | `0xffffff800241dc35` | file + IDB |
| **6** | `0x246bdb0` | **`AirLink`** | `0xffffff800241dbc0` | file + IDB xref |
| 7 | `0x246bdb8` | `SoftAP` | `0xffffff800241dc23` | file + IDB |
| 8 | `0x246bdc0` | `WiFi-Aware Discovery` | `0xffffff800241dbe4` | file + IDB |
| 9 | `0x246bdc8` | `WiFi-Aware Discovery+Data` | `0xffffff800241dbf4` | file + IDB |
| 10 | `0x246bdd0` | `WiFi-Aware Data` | `0xffffff800241dc0d` | file + IDB |

**Key table** (same indexing `role-1`, base `0xffffff800246bdd8`): roles **1–5** → `IO80211InterfaceRole` (`0xffffff800241dbab`; verified from the file on the entries of roles 1, 2 and 5 = `0x246bdd8`, `0x246bde0`, `0x246bdf8`; roles 3–4 pending read, `?`); roles **6–10** → `IO80211VirtualInterfaceRole` (`0xffffff800241dbc8`; the 5 entries at `0x246be00–0x246be20`). The literal `"IO80211InterfaceRole"` appearing in the error path of `setInterfaceRole` is the same pool string.

**Detail relevant to the AWDL gate:** the name of the property changes with the role — for roles 1–5 it is `IO80211InterfaceRole` and for roles 6–10 it is `IO80211VirtualInterfaceRole`. An AWDL interface (role 6) publishes `IO80211VirtualInterfaceRole = AirLink` on its registry entry; a role ≤ 5 interface (e.g. the low latency one, role 2) publishes `IO80211InterfaceRole = LowLatency`.

## 3) Index 2 of the table seen from `0x246bda0` = role 6 = AWDL, without enum conversion

There is no mapping function between the apple80211 role of `VIRTUAL_IF_CREATE` and the table index: **the internal enum `SkywalkInterfaceRole` uses the same numbering** as the apple80211 role (6 = AWDL/AirLink). Evidence of the chain without conversion, end to end:

1. `AppleBCMWLANCore::setVIRTUAL_IF_CREATE` (`0xffffff80016788f4`): reads the raw role from `*(request+12)` (`0xffffff800167891c`); roles 8–10 → `createNANInterface` (`0xffffff8001678952`), role **6** → `AppleBCMWLANCore::createProxInterface` (called at `0xffffff8001678be2`), role 7 → `AppleBCMWLANIO80211APSTAInterface::withOptions` (`0xffffff8001678c26`), rest → error `"Invalid virtual interface role 0x%X name %s"` (`0xffffff8001678b79`/`0xffffff8001678ba7`).
2. `AppleBCMWLANProximityInterface::withOptions(AppleBCMWLANCore*, ether_addr*, char*)` (**`0xffffff80017159e6`**, 0x8a bytes): allocates 320 B (`OSObject_typed_operator_new`, `0xffffff8001715a0e`), sets the vtable of class `off_FFFFFF80017F5B78` (`0xffffff8001715a2a`) and calls the **slot +4552 (0x11C8)** of that vtable with `(self, core, mac, **6**, name)` (`0xffffff8001715a51`: `a4: 6` literal). The slot is the specialized `init`.
3. `AppleBCMWLANProximityInterface::init(AppleBCMWLANCore*, ether_addr*, unsigned int role, char*)` (**`0xffffff8001718db0`**, 0x684 bytes): calls the parent init with the same 5 arguments via the call rendered as `off_FFFFFF80024572B8(...)` at `0xffffff8001718e09` (vtable slot whose target could not be re-verified from the file: the decoded fixup falls in `0xffffff8002305f8`, which is not a known function entry — `?`; the IDB has the fixup applied). The rest of the init (interfaceId 2 if version ≥ 5, Taskmaster, boot-args `awdl_*`) matches `AWDL_ROLE6_RE.md` §3.
4. `IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned int role, char const*)` (**entry `0xffffff80023305f8`**, 0x20c bytes; the xref to the `kalloc_type_view_341` at `0xffffff8002330629` is inside): at `0xffffff800233064b` it calls **`IO80211SkywalkInterface::setInterfaceRole(this, role)`** — without transforming the value. It writes no property here (only ivars BPF/locks/FlowQueueDatabase and boot-arg `wlan.entitlement`).
5. `setInterfaceRole(6)` (see §1) → index `6-1=5` at `0x246bdb0` → **`AirLink`**, key `0x246bdd8+5*8 = 0x246be00` → **`IO80211VirtualInterfaceRole`** → `setPropertyHelper`.

On code xrefs to the tables and the string: the code references the literals `0xffffff800246bd88` and `0xffffff800246bdd8` (real bases), not `0x246bda0`/`0x246be00`; the string `0xffffff800241dbc8` is only reached through the key table entries (matches the IDB data xrefs). The only direct code reference to `0xffffff800241dbc8` found in this pass: none besides the entries (`?` another indirect path is not ruled out).

## 4) Other data in the table's environment

- `IO80211Controller::createVirtualInterface(ether_addr*, unsigned int)` = `__ZN17IO80211Controller17createVirtualInterfaceEP10ether_addrj` at **`0xffffff800229612e`** and `attachVirtualInterface` at `0xffffff80022960aa` (matches `AWDL_ROLE6_RE.md`); `get_function_by_name` with the mangled did not resolve (IDA does not have it under that exact name), address taken from `Docs/re/io80211_25.6.0/syms.txt:11134`. Not decompiled in this pass (`?` their exact relation with `AppleBCMWLANCore::createProxInterface`).
- Vtables in the same data environment: `__ZTV17IO80211Controller` at `0xffffff8002461148`, `__ZTV23IO80211VirtualInterface` at `0xffffff8002472b78` (syms.txt:18527/18599). The role tables (`0x246bd88`/`0x246bdd8`) live in the same data region of IO80211Family.
- `IO80211SkywalkInterface::init(IOService*, ether_addr*)` = `0xffffff80022f56e2`; `getInterfaceRole` = `0xffffff80022f5ed8`; `getInterfaceRoleStr` = `0xffffff80022f5ee8` (forwarder: returns the OSString cached at `ivars+184`, calling slot +328 of the internal object — `0xffffff80022f5ef3`/`0xffffff80022f5f03`).

## 5) Method and limits

- Function addresses: project `syms.txt` + `procedure_address`/`procedure_pseudo_code` of the rea/IDA adapter (upstream ida-pro-mcp 1.30.0, base `BootKernelExtensions.kc`).
- Tables and strings: read from the `.kc` itself with the mapping `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` for the fixups (validated against two IDB anchors); the key entries of roles 3–4 (`0x246bde8`, `0x246bdf0`) and the target of slot `0xffffff80024572B8` stay `?`.
- `withOptions`/`init`/`setInterfaceRole`/`IO80211VirtualInterface::init` fully decompiled; `createProxInterface` (`0xffffff800165dcbe`, prior doc) and `createVirtualInterface` not decompiled in this pass.

## 6) Closure of the `?`s of §2 and §3 (direct file read, 2026-10-28)

Both points verified re-reading `BootKernelExtensions.kc` from the file (md5 re-verified `f489ff8be80e6013a1bf47561dcaef45`), with the already validated mapping: `fileoff = VA − 0xffffff8000100000` and fixups `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`. Control of the pass: the name entry of role 6 (`0x246bdb0`) decodes to `AirLink` and the strings at `0x231dbc0`/`0x231dbc8` read exact, same as in the IDB.

### Keys of roles 3 and 4 — the key table is now complete and uniform

| role | entry (VA) | fileoff | raw qword | low32 → string VA | string |
|---:|---|---|---|---|---|
| 3 | `0xffffff800246bde8` | `0x236bde8` | `0x004000000231dbab` | `0x231dbab` → `0xffffff800241dbab` | `IO80211InterfaceRole` |
| 4 | `0xffffff800246bdf0` | `0x236bdf0` | `0x004000000231dbab` | `0x231dbab` → `0xffffff800241dbab` | `IO80211InterfaceRole` |

Same qword byte by byte as the entries of roles 1, 2 and 5 (`0x246bdd8`, `0x246bde0`, `0x246bdf8`, re-read as control in this pass: identical). **No entry is left unread: roles 1–5 → `IO80211InterfaceRole` (`0xffffff800241dbab`), roles 6–10 → `IO80211VirtualInterfaceRole` (`0xffffff800241dbc8`); the cut of the property name falls exactly by role class, no exceptions.**

### Target of slot `off_FFFFFF80024572B8` — confirmed by direct fixup, not by inference

- Raw qword at `0xffffff80024572b8` (fileoff `0x23572b8`): **`0x00400000022305f8`** → low32 `0x22305f8` → **VA `0xffffff80023305f8`** — it falls exactly on the known entry of `IO80211VirtualInterface::init` (§3.4), not mid-function. The first bytes at the decoded fileoff decode as a standard x86_64 prologue (`55 48 89 e5 41 57 41 56 53 48 83 ec 18 41 89 cf`), consistent with a 5-argument function entry.
- The symbol at that address is `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (`Docs/re/io80211_25.6.0/syms.txt:15779`).
- Independent confirmation from the IDB: `xrefs` of rea/IDA toward `0xffffff80023305f8` lists `0xffffff80024572b8` as a data reference, next to 8 other equivalent slots (`0xffffff80017f53e0`, `0xffffff80017f69e0`, `0xffffff80017f7d00`, `0xffffff80017f9128` in `__const` of the Broadcom kext; `0xffffff800246f1e8`, `0xffffff80024739f0`, `0xffffff8002477dc0`, `0xffffff800247ad30` in IO80211Family data). The reverse query (`xrefs` to the slot) returned empty on the adapter; the reference is observed from the function side.

**Conclusion:** the call of `0xffffff8001718e09` inside `AppleBCMWLANProximityInterface::init` executes **`IO80211VirtualInterface::init` (`0xffffff80023305f8`)**, which at `0xffffff800233064b` calls `setInterfaceRole(this, role)` with the untransformed role. Additional context: slot `0xffffff80024572b8` falls inside `__ZTV19IO80211AWDLProtocol` (symbol at `0xffffff8002456440`, extending to `__ZTVN19IO80211AWDLProtocol9MetaClassE` at `0xffffff8002457618`), at +0xE78 of the symbol; its value (the init inherited from `IO80211VirtualInterface`, not an override of `IO80211AWDLProtocol`) indicates that class does not override `init`, and the dispatch through a vtable slot instead of a direct call is the kext compilation style. Why the init of `AppleBCMWLANProximityInterface` uses precisely the vtable of `IO80211AWDLProtocol` and not its own vtable was not verified (exact inheritance relation not seen in this pass, `?`). With this the chain of §3 is closed end to end with file evidence: role 6 → `AppleBCMWLANProximityInterface::init` → (slot `0x24572b8`) → `IO80211VirtualInterface::init` → `setInterfaceRole` → `IO80211VirtualInterfaceRole = AirLink`.

Note on §3.3: the prior decode registered there (`0xffffff8002305f8`) came from the old mapping, before fixing the base `0xffffff8000100000`; the raw read of this pass gives exactly `0xffffff80023305f8`.
