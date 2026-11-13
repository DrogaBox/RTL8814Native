# Small checks of stage 1: return of `IO80211VirtualInterface::init` and the stack `req` of the legacy path of the 94

Closes points 7 and 8 of §8.2 of `AWDL_INDICE.md`: the real return type of
`IO80211VirtualInterface::init` (the `?` #82 of `AWDL_STAGE1_SPEC_DELTA.md`, defect 3 of
`AWDL_STAGE1_CODE_FIXES.md`) and the initialization of the stack `req` in `setVirtualInterfaceCreate`
(finding A10 of `AWDL_STAGE1_CODE_REVIEW_HOOK.md`). Base: kernelcache of macOS 26.7.1
(25G241, x86_64); reads by IDA headless (`idat -A -S`) over a copy of `boot_awdl.kc.i64`.
Vtable offsets are relative to the vptr unless noted `ZTV+` (cell in the raw ZTV,
`ZTV+X` = `vptr+(X−0x10)`).

Checks executed for this document, in order:

| Probe | Result |
|---|---|
| Full body of `IO80211VirtualInterface::init` (`0xffffff80023305f8`), of `setVirtualInterfaceCreate` (`0xffffff8002357f33`) and of the modern handler of the 94 (`0xffffff80022886a8`), disassembly + pseudocode | §1.1, §2.1 |
| Names with the signature `4initEP17IO80211ControllerP10ether_addr` and direct code references to the base `init` | 0 direct callers in the whole KC; only the base symbol and two `kalloc_type_view` data |
| Cells pointing to `init` (9) and owner of each; content of `0xffffff80017DE1C0` | §1.2 |
| References to the two `init`s of `AppleBCMWLANProximityInterface` and super-call sites via the ZTV cell of `IO80211AWDLProtocol` | §1.3 |
| Body of `AppleBCMWLANProximityInterface::withOptions` | §1.3 |

## 1. Point #7: return type of `IO80211VirtualInterface::init`

### 1.1 The body: only the low byte ends up defined

Signature via the symbol:
`__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc`,
head `0xffffff80023305f8`, function `0xffffff80023305f8`–`0xffffff8002330804`. All exits
converge on the epilogue `0xffffff800233079b` (stack cookie) and return through
`0xffffff80023307ab`.

| VA | Instruction / line | Fact |
|---|---|---|
| `0xffffff8002330608` | `mov r14, rsi` | r14 = 2nd argument (the `IO80211Controller*` per the symbol); not rewritten until `0xffffff8002330740` (only read at `0xffffff8002330690` and `0xffffff80023306a1`) |
| `0xffffff8002330740` | `mov r14b, 1` | success branch: **only the low byte** is written; bits 8-31 of r14d keep the residue of the controller pointer |
| `0xffffff8002330743`–`0xffffff8002330746` | `test rax, rax` ; `jnz 0xffffff800233079b` | success requires `IO80211FlowQueueDatabase::initialize` non-null (saved at ivars+0x200 in `0xffffff8002330739`) |
| `0xffffff8002330767`, `0xffffff8002330787`, `0xffffff8002330798`, `0xffffff80023307c7`, `0xffffff80023307df`, `0xffffff80023307f0` | `xor r14d, r14d` | the six failure branches (flow queue base, `super::init`, ivars of `IOMallocTypeImpl`, `_bpfLock`, `_bpfListHead`, denied processes dictionary) leave the value at 0 |
| `0xffffff80023307ab` | `mov eax, r14d` | return: on success eax = low-32 of the controller pointer with the low byte at 1; on failure eax = 0; bits 32-63 of rax are zeroed by the 32-bit write |

The pseudocode reproduces the same: `LOBYTE(v6) = 1;` is the only write of the success branch,
each failure does `LODWORD(v6) = 0;`, and the function ends in `return (unsigned int)v6;` with v6
initialized to the 2nd argument — the decompiler shows the pointer residue in the high bits.

Code consequence: under a 4- or 8-byte return contract the compiler would have to leave
eax (or rax) defined on the success branch; leaving the controller pointer residue there is only
valid if the type declared by the family itself is **1 byte**. The binary guarantees exactly
`al`: 1 on success, 0 on failure. A 64-bit reader would depend on unspecified bits
(on failure, a non-null residue in bits 32-63 would give a false success); the enforceable
contract is to read the byte.

### 1.2 The virtual slot: cell `vptr+0xE68`, with no override anywhere in the KC

`__ZTV23IO80211VirtualInterface` is at `0xffffff8002472b78` and the cell `ZTV+0xE78`
(= `vptr+0xE68`) contains `0xffffff80023305f8`, the 4-argument `init`. The neighboring cells close
the alignment: `ZTV+0xE70` = `IO80211SkywalkInterface::syncDPSStats`
(`0xffffff80022fa22e`) and `ZTV+0xE80` = `IO80211VirtualInterface::setLinkState`
(`0xffffff8002333408`).

Nine vtables carry the same cell pointing to the base implementation:

| ZTV | Cell | Content |
|---|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff80024739f0` | `0xffffff80023305f8` |
| `__ZTV19IO80211AWDLProtocol` | `0xffffff80024572b8` | `0xffffff80023305f8` |
| `__ZTV18IO80211SapProtocol` | `0xffffff800246f1e8` | `0xffffff80023305f8` |
| `__ZTV22IO80211NANDataProtocol` | `0xffffff8002477dc0` | `0xffffff80023305f8` |
| `__ZTV18IO80211NANProtocol` | `0xffffff800247ad30` | `0xffffff80023305f8` |
| `__ZTV33AppleBCMWLANIO80211APSTAInterface` | `0xffffff80017f53e0` | `0xffffff80023305f8` |
| `__ZTV30AppleBCMWLANProximityInterface` | `0xffffff80017f69e0` | `0xffffff80023305f8` |
| `__ZTV24AppleBCMWLANNANInterface` | `0xffffff80017f7d00` | `0xffffff80023305f8` |
| `__ZTV28AppleBCMWLANNANDataInterface` | `0xffffff80017f9128` | `0xffffff80023305f8` |

None overrides it: the 4-argument `init` is a **virtual method** of
`IO80211VirtualInterface` at slot `0xE68` and no override of it exists in the whole kernelcache;
every instance reaches the base implementation. The slot matches the one the local header
declares (`AWDLInterface.h:149`, `virtual bool init(...) /* 0xE68 */`), consistent with the
vtable order check already closed without discrepancies.

### 1.3 The consumers: all read `al`

Birth chain of the Proximity interface, with the return consumption at each level:

| Level | Addresses | Consumption |
|---|---|---|
| `AppleBCMWLANProximityInterface::withOptions` (`0xffffff80017159e6`) | allocates with `OSObject_typed_operator_new` (`0xffffff8001715a09`), sets the vptr (`0xffffff8001715a23`–`0xffffff8001715a2a`), dispatches `call qword ptr [rax+11C8h]` (`0xffffff8001715a49`, its own virtual init of Proximity, cell `0xffffff80017f6d40`; role 6 travels in ecx, `0xffffff8001715a41`) | `test al, al` at `0xffffff8001715a4f`; on failure `call qword ptr [rax+28h]` (`0xffffff8001715a59`) and null return |
| `AppleBCMWLANProximityInterface::init(AppleBCMWLANCore *,ether_addr *,uint,char *)` (`0xffffff8001718db0`) | `mov rax, cs:off_FFFFFF80017DE1C0` (`0xffffff8001718deb`; its content is `0xffffff8002456440`, `vtable for'IO80211AWDLProtocol`) and `call qword ptr [rax+0E78h]` (`0xffffff8001718e01`, the cell `__ZTV19IO80211AWDLProtocol+0xE78` = `init`) | `test al, al` at `0xffffff8001718e07`; `jz 0xffffff8001719185` → `xor r14d, r14d` and return 0 |
| `IO80211VirtualInterface::init` (`0xffffff80023305f8`) | `call IO80211SkywalkInterface::init` at `0xffffff800233061c` | `test al, al` at `0xffffff8002330621`, `jz` to the failure branch |

The Proximity init itself returns with the same byte pattern: `setnz r14b`
(`0xffffff8001719236`) and `mov eax, r14d` (`0xffffff80017192df`). The pseudocode types the three
levels as booleans (cast `unsigned __int8` in the two by-cell calls and `bool` in the recovered
signatures).

### 1.4 Conclusion

The real return type of `IO80211VirtualInterface::init(IO80211Controller *, ether_addr *,
uint32_t, const char *)` is a **1-byte `bool`**: the body only defines `al` (1 on success, 0 on
failure; `0xffffff8002330740`, `0xffffff80023307ab`), the three observed consumers read it with
`test al, al`, and the virtual cell `0xE68` points to this implementation in the nine vtables of
the KC without override. The stage 1 code already complies: `rtl_vif_init` declared `bool`
(`AWDLInterface.cpp:47`) and checked (`AWDLInterface.cpp:114`), header with the slot
(`AWDLInterface.h:149`) — defect 3 of `AWDL_STAGE1_CODE_FIXES.md` is validated by the
binary, and with it the `?` #82 of `AWDL_STAGE1_SPEC_DELTA.md` is closed.

`?` The creators dispatching cell `0xE68` by vptr outside the Proximity/withOptions chain
(APSTA, NAN, NANData, Sap, NANProtocol) were not swept; it is irrelevant for the type, because the
implementation is unique across the nine vtables.

## 2. Point #8: the stack `req` of the legacy path of the 94

### 2.1 The function and its buffer

Entry 93 of the legacy table is `setVirtualInterfaceCreate`:
`_gSetHandlerTableLegacy` at `0xffffff8002479060`, entry [93] at `0xffffff8002479348`, content
`0xffffff8002357f33` (`dq offset __ZL25setVirtualInterfaceCreateP17IO80211ControllerP23IO80211SkywalkInterfaceP13apple80211reqb`),
function `0xffffff8002357f33`–`0xffffff8002358034`.

| VA | Instruction / line | Fact |
|---|---|---|
| `0xffffff8002357f52`–`0xffffff8002357f60` | `xor eax, eax` + 4 × `mov [rbp+var_38/var_40/var_48/var_50], rax` | the 32 bytes of the stack req (`_QWORD v12[4] // [rsp+10h] [rbp-50h]` in the pseudocode, which shows it as `memset(v12, 0, sizeof(v12));`) are zeroed **before** copying anything |
| `0xffffff8002357f64`, `0xffffff8002357f6e` | `mov ecx, [rdx+18h]` ; `jz` → return 22 | len of the external req; len 0 → 22 without touching anything |
| `0xffffff8002357f7a`, `0xffffff8002357f81` | `mov rsi, [rdx+20h]` ; `jz` → 22 | payload pointer; null → 22 |
| `0xffffff8002357f8a`–`0xffffff8002357f93` | `cmp ecx, 20h` ; `mov r12d, 20h` ; `cmovb r12d, ecx` | n = min(len, 32) |
| `0xffffff8002357f97`–`0xffffff8002357f9e` | `lea rdx, [rbp+var_50]` ; `mov rcx, r12` ; `call IO80211Controller::copyIn` | copies n bytes of the payload pointed by req+0x20 into the stack req |
| `0xffffff8002357fc7`, `0xffffff8002357fda` | `mov r8d, 20h` ; `call IO80211Glue::sendIOUCToWcl(..., 802869C8h, 5Eh, &v12, 20h, &consumed)` | the WCL probe always receives length 0x20, with the consumption mark at 0 (`0xffffff8002357fbe`) and tested at `0xffffff8002357fdf` |
| `0xffffff8002357fed`–`0xffffff8002357ff4` | `lea rsi, [rbp+var_50]` ; `call apple80211setVIRTUAL_IF_CREATE` | the bridge receives (interface, pointer to the stack req) — **without a length argument** |
| `0xffffff80022886c8`–`0xffffff80022886cc` | `cmp dword ptr [rcx+18h], 20h` ; `jnz` → 22 | the len == 32 requirement lives in the modern handler `setVIRTUAL_IF_CREATE` (`0xffffff80022886a8`), which the legacy path does **not** go through: the only own filter of the legacy is len ≠ 0 |
| `0xffffff8002357ffd`–`0xffffff800235800b` | `mov rdx, [r14+20h]` ; `mov rcx, r12` ; `call IO80211Controller::copyOut` | copies back to the sender the same n |

### 2.2 Conclusion

The «stack req» is the 32-byte payload (`apple80211_virt_if_create_data`, layout of
`AWDL_STRUCTS.md` §3.2) and **is born entirely zeroed**: the initialization covers the 32 bytes
(`0xffffff8002357f52`–`0xffffff8002357f60`) and precedes the copy. The only lengths in play
are n = min(len, 32) for `copyIn`/`copyOut` and 0x20 for the WCL probe; the buffer carries no
own length field.

With `len < 32`, the bytes the copy does not fill stay zero: `role` (u32 at +8) and `ifname`
(char[16] at +0x10) read deterministic values — sender bytes up to len and zeros from there —,
never uninitialized memory. The premise of A10 is confirmed (the legacy accepts len < 32 and
reaches the bridge without the 0x20 check, `0xffffff8002357ff4`), but the uninitialized-bytes
suspicion dissolves: the zeroing is the family's defense. The severity of A10 drops from
«incorrect behavior (incomplete sender only)» to harmless for the driver: a truncated role
will not be 6 and the stage 1 override rejects it by role (`AWDL_STAGE1_SPEC_DELTA.md` #28), and an
empty name publishes no name. The contract limit A10 records stands: the bridge receives no
length, so the override cannot detect the truncation.

`?` Which real senders use `len < 32` has not been swept; the daemon sends 32
(`AWDL_STRUCTS.md` §3.2, cited by A10), so the case stays hypothetical.

## 3. Open points after this document

| # | Point | Status |
|---:|---|---|
| 7 | Return type of `IO80211VirtualInterface::init` (`AWDL_INDICE.md` §8.2; #82 of SPEC_DELTA) | **Closed**: 1-byte `bool` (§1.4); code already compliant (defect 3 applied). `?` residue: creators by vptr outside the Proximity chain not swept, no effect on the type |
| 8 | Initialization of the legacy stack `req` with `len < 32` (A10) | **Closed**: 32 bytes zeroed before the `copyIn`; n = min(len, 32); the bridge receives no length (§2.2). A10 drops to harmless. `?` residue: real senders with `len < 32` (the daemon sends 32) |
