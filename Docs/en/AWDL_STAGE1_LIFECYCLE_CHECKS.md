# Lifecycle checks: destination of the tx data offset, the `deregister` uint parameter and Broadcom `start`'s `r15`

It closes points 4, 5 and 7 of the section "Points left open" of `AWDL_STAGE1_PENDING_CHECKS.md`
(grouped under #9 of §8.2 of `AWDL_INDICE.md`): the `ifnet` field consuming the tx data
offset after `getTxDataOffset`, the intent of the `uint` parameter of
`deregisterEthernetInterface`/`deregisterNetworkInterface` and the binding of `r15` in
`AppleBCMWLANSkywalkInterface::start`. Base: kernelcache of macOS 26.7.1 (25G241, x86_64); reads
by IDA headless (`idat -A -S`) over a copy of `boot_awdl.kc.i64` in `Build/ida_awdl/task_lifecycle/`.
The vtable offsets are relative to the vptr unless marked `ZTV+` (cell in the raw ZTV,
`ZTV+X` = `vptr+(X−0x10)`). Each claim was read in the disassembly or pseudocode; anything
unseen carries `?`.

Checks executed for this document, in order:

| Sweep | Result |
|---|---|
| Body of `IOSkywalkNetworkBSDClient::start` (`0xffffff8002aff562`–`0xffffff8002affce6`) and of `gatedPrepareNexus` (`0xffffff8002b0066c`–`0xffffff8002b006ed`), disassembly + pseudocode | §1.1, §1.2 |
| Sweep of displacement `13Ch` over functions of range `0xffffff8002900000`–`0xffffff8002e00000` and with Skywalk/BSDClient names (106 accesses) | §1.1 |
| Sweep of `144h`/`146h` and of `58Ch` over `_ifnet*`/`_dlil*`/`*netif*`/nexus functions and over the text `0xffffff8000400000`–`0xffffff8002600000` | §1.3, §1.4 |
| Pseudocode of the BSD client's `start`, of the three `initBSDInterfaceParameters`, of `kern_nexus_controller_alloc_net_provider_instance` and of `nx_netif_default_llink_config` | §1.2, §1.3 |
| Pseudocode of `ifnet_allocate_extended` (`0xffffff800063f3f0`–`0xffffff800063ff61`) | §1.3, §1.4 |
| Full bodies of the two `deregister*`, of `stop`, `deregisterLogicalLink` and `destroySkywalkInterface`; cell `vptr+0x600` of the Skywalk/AppleBCMWLAN vtables; xrefs of the `deregister*` | §2 |
| Disassembly of `AppleBCMWLANSkywalkInterface::start` (`0xffffff80015d909a`–`0xffffff80015d9756`), of `AppleBCMWLANLowLatencyInterface::start` and of `createSkywalkInterface`; sweep of `14E0h` | §3 |

## 1. Point #4: the tx data offset ends at `ifnet+0x58C`, not in the BSD client

### 1.1 The corrected premise: `0xffffff8002affa94` writes into a stack struct

`IOSkywalkNetworkBSDClient::start` (head `0xffffff8002aff562`) reads the tx data offset through slot
`0x9C0` and saves it, but the store's receiver is not the client:

| VA | Instruction | Fact |
|---|---|---|
| `0xffffff8002affa0c`–`0xffffff8002affa1b` | `lea r13, [rbp+var_278]` / `mov esi, 150h` / `call _bzero` | r13 = the stack struct's body (0x150 bytes), zeroed |
| `0xffffff8002affa20`–`0xffffff8002affa2a` | `mov rax, 15800000002h` / `mov [r13-8], rax` | struct header: dword `+0` = 2, dword `+4` = 0x158 |
| `0xffffff8002affa7c` | `mov dword ptr [r13+0], 80000000h` | proof r13 is not the client: in the client `+0` is the vtable |
| `0xffffff8002affa84`–`0xffffff8002affa94` | `mov rdi, [rbx+0E8h]` / `call [rax+9C0h]` / `mov [r13+13Ch], ax` | slot `0x9C0` runs on the interface (rbx = this, `[client+0xE8]` = the interface since `0xffffff8002aff696`–`0xffffff8002aff69b`) and the returned u16 goes to the stack struct |
| `0xffffff8002affa9c`–`0xffffff8002affaa7` | `mov rax, [rbx+0F0h]` / `movzx eax, word ptr [rax+3Eh]` / `mov [r13+13Eh], ax` | the u16 of `RegInfo+0x3E` goes to the sibling field; `[client+0xF0]` is the interface's `RegistrationInfo` copy (`0xffffff8002aff73c`–`0xffffff8002aff74d`: `mov rax, [rdi+0C0h]; mov rax, [rax]`) |

The client's dword at `+0x13C` exists and is another field, a state `setPowerStateHandler` writes:

| VA | Function | Access |
|---|---|---|
| `0xffffff8002aff551` | `IOSkywalkNetworkBSDClient::init` | `mov dword ptr [rbx+13Ch], 1` |
| `0xffffff8002b00091` / `0xffffff8002b000e4` / `0xffffff8002b000f9` | `setPowerStateHandler` | dword writes 0 / 2 / 1 |
| `0xffffff8002affdb2` / `0xffffff8002affddd` | `ioctlHandler` | `cmp`/`mov eax, dword ptr [rbx+13Ch]` |
| `0xffffff8002b0017a` | `reportInterfaceWakeFlags` | `cmp dword ptr [rbx+13Ch], 0` |

Partial conclusion: the headroom never reaches the client object; `PENDING_CHECKS` §1 is corrected.
What the `start` fills is the `ifnet`'s initialization struct.

### 1.2 The struct is the `ifnet_init_eparams`

The stack struct (base `rbp-280h`: the header `{2, 0x158}` occupies its first 8 bytes and the 0x150-byte
body starts at `rbp-278h`) is an `ifnet_init_eparams`: cell
`vptr+0x8E0` of the interface vtables declares it (`__ZN26IOSkywalkEthernetInterface26initBSDInterfaceParametersEP18ifnet_init_eparamsPP11sockaddr_dl`,
`0xffffff8002b10bca`; on `__ZTV28AppleBCMWLANSkywalkInterface` it is covered by
`IO80211InfraInterface::initBSDInterfaceParameters`, `0xffffff800235ff5a`), and its body validates the
header: `*(_DWORD *)a2 == 2 && *(_DWORD *)(a2 + 4) == 344` (pseudocode of `0xffffff8002b10bca`).
The `start` calls the cell at `0xffffff8002affacb`–`0xffffff8002affadc` with r13 reloaded to the base.

| eparams field | Write in `start` | Content |
|---|---|---|
| `+0x00`/`+0x04` | `0xffffff8002affa20` | `{2, 0x158}` |
| `+0x08` | `0xffffff8002affa7c` | `0x80000000` (dword; the alloc requires `& 3 == 0`) |
| `+0x20` | `0xffffff8002affa3e` (`[r13+18h]`) | BSD prefix pointer (slot `+0x9E8` of the interface, "awdl" for role 6) |
| `+0x28` | `0xffffff8002affa42` (`[r13+20h]`) | unit (u32) |
| `+0x30` | `initBSDInterfaceParameters` (`*(_DWORD *)(a2 + 48) = 6`) | type 6 (IFT_ETHER) |
| `+0x38` | `0xffffff8002affa54` | `IOSkywalkNetworkBSDClient::if_output` |
| `+0x108` / `+0x118` | `0xffffff8002affa5f` / `0xffffff8002affa6d` | `if_ioctl` / `if_detach` |
| `+0x144` | `0xffffff8002affa94` | **tx data offset** (u16, return of slot `0x9C0`) |
| `+0x146` | `0xffffff8002affaa7` | u16 of `RegInfo+0x3E` |
| `+0x148` | `0xffffff8002affaec` (`[rbp+var_138]` = `[client+0x130]`, with reserve 300/0) | u32 |

The eparams travels to the alloc inside stack struct `var_128` (`v51[1] = &__b` in the pseudocode;
`mov [r15+8], r13` at `0xffffff8002affb31`), next to the `sockaddr_dl` that `initBSDInterfaceParameters`
creates on `RegInfo+0x1E` itself (`*a3 = (char *)v10 + 30`, with the MAC of `RegInfo+0x108`
at `a2+16` and length 6 at `a2+24`). `kern_nexus_controller_alloc_net_provider_instance`
(`0xffffff80009a4140`) extracts the pointer with `v14 = *(_QWORD *)(a6 + 8)`, checks
`*(v14 + 72) == 0` (`+0x48`) and `(*(_BYTE *)(v14 + 8) & 3) == 0`, and calls
`ifnet_allocate_extended(v14, &interface)` (site `0xffffff80009a420d`, also cited by
`AWDL_STAGE1_TXPUSH_LLINK.md` §5).

### 1.3 `ifnet_allocate_extended`: from `eparams+0x144` to `ifp+0x58C`

`_ifnet_allocate_extended` (head `0xffffff800063f3f0`, end `0xffffff800063ff61`) is the only
reader of `word ptr [reg+144h]`/`[reg+146h]` from the sweep over `_ifnet*`/`_dlil*`/`*netif*`/nexus functions
(the sweep's third hit, `mov al, [r13+146h]` in `nxp_qset_init`, `0xffffff8002aff256`, is the byte
of another structure):

| VA | Instruction | Fact |
|---|---|---|
| `0xffffff800063f626` | `movzx r15d, word ptr [rdi+144h]` | read of the tx data offset |
| `0xffffff800063f62e` | `movzx eax, word ptr [rdi+146h]` | read of the sibling u16 |
| `0xffffff800063f639` | `mov eax, [rdi+148h]` | read of the u32 |
| `0xffffff800063f669`–`0xffffff800063f689` | `lea ecx, [rbx-1]` / `cmp ecx, 0FFh` / `test r15b, 7` / `setz` / `jnz` | joint validation (pseudocode: `strlen(name) >= 0x10 || (v6-1) >= 0xFF || (v7 & 7) != 0` → 22): **the tx data offset must be a multiple of 8** |
| `0xffffff800063f677` / `0xffffff800063fd42` | `mov [rbp+var_84], r15d` / `mov eax, [rbp+var_84]` | the value reaches the store |
| `0xffffff800063fd48` | `mov [r13+58Ch], ax` | **write into the new `ifnet` (r13 = the ifp of `dlil_if_acquire`): field `+0x58C`, u16** |
| `0xffffff800063fd54` | `mov [r13+58Eh], ax` | sibling field `+0x58E` |
| `0xffffff800063fd62` | `mov [r13+590h], eax` | u32 at `+0x590` |
| `0xffffff800063fce8` | `mov qword ptr [r13+58Ch], 0` | error path: the trio is nulled |

Layout calibration against named KPIs: `_ifnet_set_hdrlen` writes the byte at
`[rdi+0E4h]` (`0xffffff8000641119`) and `_ifnet_set_mtu` the u32 at `[rdi+0E8h]`
(`0xffffff8000641079`), so `if_data` starts at `0xE0` and `0x58C`/`0x58E`/`0x590` fall outside:
they are an `ifnet`-own trio for the data offsets. The declared name of those fields in
xnu's `if_var.h` appears neither in the SDK (Kernel.framework) nor in MacKernelSDK; the field is fixed
by offset and by its consumer (`?` on the name).

### 1.4 The consumer: `nx_netif_host_output`

The sweep of `58Ch` over the whole kernel text gives these readers of the field:

| VA | Function (head) | Use |
|---|---|---|
| `0xffffff80009ea796` / `0xffffff80009ea7b6` | `_nx_netif_host_output` (`0xffffff80009ea590`) | two u16 reads: `add eax, r9d` and `cmp` against the limit (`0xffffff80009ea7a1`–`0xffffff80009ea7a8`), then `lea esi, [r9+r14]` and `cmp` against `word [rbx+28h] * dword [rbx+20h]` (`0xffffff80009ea7cd`–`0xffffff80009ea7d9`), and `pp_alloc_packet_by_size` (`0xffffff80009ea80c`): the netif reserves in the nexus packet the size that includes the tx data offset when converting the stack's mbuf |
| `0xffffff80009bd035` | `_fsw_ring_flush` (`0xffffff80009bb8a0`) | same capacity arithmetic; excess ⇒ error `0x22` |
| `0xffffff80009f870d` | `sub_FFFFFF80009F86C0` (the same one calling `netif_transmit`, `AWDL_STAGE1_TXPUSH_LLINK.md` §3) | reads the field, adds byte `[rdx+3Eh]` to it, checks parity and uses `word [rdx+3Ah]`: packet-building arithmetic |
| `0xffffff8000bb2d9d` / `0xffffff8000bb2fe2` | `sub_FFFFFF8000BB2D3B` (no symbol) | two u16 reads |
| `0xffffff80009f59c0` / `0xffffff80009f61fd` / `0xffffff80009f6955` | `_nx_netif_mbuf_to_filter_pkt_chain` / `_nx_netif_pkt_to_filter_pkt` / `_nx_netif_pkt_to_pkt` | byte reads of the same field |

Point #4's conclusion: the full chain is
`RegInfo (interface copy) +0x3C` → slot `0x9C0` (`getTxDataOffset`, `0xffffff8002b0baba`) →
`ifnet_init_eparams+0x144` (stack of the BSD client's `start`, `0xffffff8002affa94`) →
multiple-of-8 validation (`0xffffff800063f67e`) → **`ifnet+0x58C` (u16, `0xffffff800063fd48`)** →
`nx_netif_host_output`, which sizes the TX packets with it. The BSD client never stores the
value; its dword `+0x13C` is `setPowerStateHandler`'s state.

## 2. Point #7: the `uint` of `deregister` is not read on any route

### 2.1 The two bodies

`IOSkywalkEthernetInterface::deregisterEthernetInterface(uint)` (head `0xffffff8002b10af4`,
end `0xffffff8002b10b45`): only register use `mov rbx, rdi` (`0xffffff8002b10afe`); it forwards `esi`
intact to `IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (`0xffffff8002b10b01`), and if
it returns 0 it frees the block hanging from `[this+0x118]` with `IOFreeTypeImpl`
(`0xffffff8002b10b22`, `kalloc_type_view_239`) and nulls it.

`IOSkywalkNetworkInterface::deregisterNetworkInterface(uint)` (head `0xffffff8002b09e9c`,
end `0xffffff8002b09ffd`): full body without a single read of `esi`:

| VA | Fact |
|---|---|
| `0xffffff8002b09eba` | `removeNonDefaultLogicalLinks` |
| `0xffffff8002b09ec9`–`0xffffff8002b09ee4` | `lastObject` (cell `+0x1D8`) and `safeMetaCast` to `IOSkywalkLogicalLink` |
| `0xffffff8002b09f0c`–`0xffffff8002b09f29` | loop `getQueueSet(i)` + `removeSteeringRule` over `[link+28h]` |
| `0xffffff8002b09f30` | `setInterface(link, null)` |
| `0xffffff8002b09f41`–`0xffffff8002b09fbb` | release and null of `+0xC8`, `+0xA0`, `+0x98`, `+0x90`, `+0xD0` |
| `0xffffff8002b09fd5`–`0xffffff8002b09fee` | `IOFreeTypeImpl` of the `RegInfo` copy at `+0xC0` (`kalloc_type_view_401`) |
| `0xffffff8002b09ff1` | `xor eax, eax` (return 0) — it is the function's own return, not a separate "base implementation" as `PENDING_CHECKS` §3 said |

### 2.2 Callers: all the xrefs

| Caller | Site | `esi` value |
|---|---|---|
| `registerEthernetInterface` (packet-queue variant, `0xffffff8002b10a84`) | `0xffffff8002b10ab5` | 0 (`xor esi,esi` at `0xffffff8002b10ab3`, failure path of `copyRegistrationInfo`) |
| `registerEthernetInterface` (logical-link variant, `0xffffff8002b10b46`) | `0xffffff8002b10b71` | 0 (`xor esi,esi` at `0xffffff8002b10b6f`) |
| `AppleEthernetRL::stopInterface` | `0xffffff800295c05f` (tail with `jmp`) | 0 (`xor esi,esi` at `0xffffff800295c053`) |
| `IOSkywalkNetworkInterface::stop` (head `0xffffff8002b09e66`) | `0xffffff8002b09e80` | **untouched**: keeps the low 32 bits of the provider pointer (2nd parameter of `stop`, saved in rbx at `0xffffff8002b09e6d` for the super-call of `0xffffff8002b09e96`) |
| `IOSkywalkNetworkInterface::free` (head `0xffffff8002b09ffe`) | `0xffffff8002b0a007` | **untouched**: residue of `free`'s caller |
| `deregisterLogicalLink` (head `0xffffff8002b0a7d4`) | `0xffffff8002b0a7d9` (pure `jmp`) | its caller's; its 21 references are vtable cells (data, all preceded by `registerNetworkInterfaceWithLogicalLink` in the previous cell), without any direct code call in the KC |

The kernel itself passes an uncontrolled residue on two of the five sites: the parameter is
operationally dead.

### 2.3 The destroy call with `esi=2` is `terminate`

`AppleBCMWLANCore::destroySkywalkInterface(uint)` (head `0xffffff80016aae4e`) calls at
`0xffffff80016aaeb4`–`0xffffff80016aaebc` with `mov esi, 2` cell `vptr+0x600`, which is not
`deregister`: it is `IOService::terminate(uint)` (`0xffffff8000a937b0`) in
`__ZTV28AppleBCMWLANSkywalkInterface`, `__ZTV25IOSkywalkNetworkInterface`,
`__ZTV26IOSkywalkEthernetInterface` and the rest of the family, and
`IO80211VirtualInterface::terminate` (`0xffffff8002330c50`) in
`__ZTV30AppleBCMWLANProximityInterface`, `__ZTV24AppleBCMWLANNANInterface`,
`__ZTV28AppleBCMWLANNANDataInterface` and `__ZTV33AppleBCMWLANIO80211APSTAInterface`; afterward
it goes through cell `+0x90` (release) at `0xffffff80016aaed7`. By-reference destruction never passes
through `deregister` anywhere, as `PENDING_CHECKS` §3 already anticipated.

### 2.4 Conclusion

The `uint` is not read in either function nor on any route of the KC of 25G241: the three
controlled direct callers pass 0, `stop` and `free` pass residue, and the only destroy call with a
non-null value (`esi=2`) goes to `terminate`. Operational contract for a driver: pass 0;
the failed registration's cleanup is covered by `free`, which calls `deregisterNetworkInterface` unconditionally at
the start (`0xffffff8002b0a007`). `?` The parameter's declared intent is not deducible from the
binary: neither the symbol (`…Ej`, unnamed `unsigned int`) nor the available headers fix it.

## 3. Point #5: `r15` in `AppleBCMWLANSkywalkInterface::start` is the caller's `RegistrationInfo`

Head `0xffffff80015d909a`; symbol
`__ZN28AppleBCMWLANSkywalkInterface5startEP16AppleBCMWLANCorePN26IOSkywalkEthernetInterface16RegistrationInfoE`
→ `start(AppleBCMWLANCore *, IOSkywalkEthernetInterface::RegistrationInfo *)`.

| VA | Instruction | Fact |
|---|---|---|
| `0xffffff80015d90ae` | `mov r15, rdx` | r15 = 3rd argument register = 2nd nominal parameter, the `RegistrationInfo *` (r14 = rsi = `AppleBCMWLANCore *`, rbx = rdi = this) |
| `0xffffff80015d92b4` | `movzx eax, word ptr [r15+3Ch]` / `mov [r12+50h], ax` | first read of the headroom, into the registration struct on the bus (`r12 = rbp+bytes`) that goes to the call of `0xffffff80015d930d` |
| `0xffffff80015d947a` | `mov [rbp+var_C8], r15` | pointer copy before reusing the register |
| `0xffffff80015d94dc` | `movzx r15d, byte ptr [r13+2Ah]` | r15 reused (queue count) |
| `0xffffff80015d9518` | `mov r15, [rbp+var_C8]` | exact restoration of the pointer |
| `0xffffff80015d9583` | `movzx eax, word ptr [r15+3Ch]` / `mov [rdx+28h], ax` | second read; the value ends in the ivars (`rdx = [rbx+138h]`) |
| `0xffffff80015d9593` / `0xffffff80015d9597` | `sub [r15+48h], eax` / `sub [r15+4Ch], eax` | **the two MTUs of the `RegInfo` are reduced by the headroom before registering** (Broadcom fixes them to 0x594 at `0xffffff80017296ff`–`0xffffff8001729709`, the pair the local header calls `AWDL_REGINFO_MTU`) |
| `0xffffff80015d95b4` | `call IO80211InfraInterface::registerInfraEthernetInterface` | rsi = r15: the already-mutated `RegInfo` |

Links of the function and the argument:

- Virtual method, last cell of the vtable: `__ZTV28AppleBCMWLANSkywalkInterface` at
  `ZTV+0x14F0` (`0xffffff80017e3b18`), same position in `__ZTV31AppleBCMWLANLowLatencyInterface`
  (`0xffffff80017e5980`).
- Dispatcher: `AppleBCMWLANCore::createSkywalkInterface(AppleBCMWLANSkywalkInterface **,
  apple80211SkywalkIfCreateParams_t *, IO80211VirtualInterface *)` (head `0xffffff80016aa874`),
  which at `0xffffff80016aac2f`–`0xffffff80016aac46` arms the stack `RegInfo`
  (`lea rdx, [rbp+var_160]`), dispatches `call qword ptr [rax+14E0h]` with rdi = the new interface and
  rsi = the core, and checks `test al, al`.
- Only direct code caller in the KC: `AppleBCMWLANLowLatencyInterface::start`
  (head `0xffffff80015e00bc`), tail with `jmp` at `0xffffff80015e00f6` passing its own 3rd
  parameter in rdx. Its pseudocode does `setInitMacAddress(this, a3 + 44)` with `a3` typed
  `ether_addr *`: 44 × 6 bytes = `+0x108`, the `RegInfo` MAC's offset (the same one
  `initBSDInterfaceParameters` reads at `RegInfo+264`), confirming the object's identity.

Conclusion: `r15` is taken from `rdx` at the head itself (`0xffffff80015d90ae`) and binds the
`RegistrationInfo` the caller hands over — the caller's original object, not the
family's copies at `iface+0xC0`/`+0x118`, which are created later inside `registerNetworkInterface`. The two
reads of `+0x3C` of `PENDING_CHECKS` §1 are thus justified instruction by instruction, and an
undocumented effect appears: the family mutates the caller's `RegInfo` by subtracting the headroom from its two
MTUs.

## 4. Corrections this walk introduces

| Document | Previous claim | Correction |
|---|---|---|
| `AWDL_STAGE1_PENDING_CHECKS.md` §1 | "it saves the u16 result at `[BSDClient+0x13Ch]` … the headroom reaches the BSD client this way" | the store of `0xffffff8002affa94` goes to the stack `ifnet_init_eparams` (`+0x144`); the client never saves the value; its dword `+0x13C` is the state written by `init`/`setPowerStateHandler` (§1.1) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §1, open point #4 | consumer `ifnet` field untraced | closed: `ifnet+0x58C` (u16), consumed by `nx_netif_host_output` (§1.3, §1.4) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §1, open point #5 | `r15` binding unfixed | closed: `r15 = rdx` at `0xffffff80015d90ae`, the caller's `RegistrationInfo` (§3) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §3 | "the base implementation does not use it either and always returns 0 (`0xffffff8002b09ff1`)" | `0xffffff8002b09ff1` is `deregisterNetworkInterface`'s own return; there is no second implementation (§2.1) |
| `AWDL_STAGE1_PENDING_CHECKS.md` §3, open point #7 | `uint` intent not deduced | stays `?`, now with the full caller inventory and the two residues (`stop`, `free`); the destroy call with `esi=2` identified as `terminate` (§2.2, §2.3) |
| `AWDL_STAGE1_BSDSTART_ORDER.md` §2.4 | "`0xffffff8002affa0c`–`0xffffff8002affab7` \| `ifnet` parameters on the stack (`var_128`)" | that window fills the `ifnet_init_eparams` (base `rbp-280h`, header included); struct `var_128` (`{1, eparams, sockaddr_dl, callback, pools, llink}`) is built afterward, at `0xffffff8002affb23`–`0xffffff8002affb79` (§1.2) |

## 5. Application to stage 1

- The headroom 64 of `RegInfo+0x3C` (`AWDLInterface.cpp:196`) reaches `ifp+0x58C` without driver
  intervention: the family reads it through slot `0x9C0` during `bsd_start` and hands it to the `ifnet`. The
  validation of `ifnet_allocate_extended` requires a multiple of 8 (64 complies); defect 6
  (`+0x3A` uncorrected) would leave the field at 0.
- `RegInfo+0x3E` travels to sibling field `ifp+0x58E` (`0xffffff800063fd54`); stage 1 leaves it at 0
  (the registration's `bzero`), same as Proximity's reference, which only writes `+0x3C`.
- Observed difference with the reference: `AppleBCMWLANSkywalkInterface::start` subtracts the headroom from
  the `RegInfo`'s two MTUs before registering (`0xffffff80015d9593`, `0xffffff80015d9597`), and the netif
  adds the tx data offset by itself when sizing the output packets (§1.4). Stage 1
  registers MTU 0x594 with headroom 64 without the subtraction (`AWDLInterface.cpp:198`). `?` The exact
  consequence on the kext's route is not measured: stage 1 does not cross that Broadcom `start`, and
  replicating the registered MTU adjustment (net payload) would be needed if T3 shows size discards.
- `deregisterEthernetInterface`: the failed registration's cleanup is covered by `free` (`0xffffff8002b0a007`);
  if the explicit function is called (the `AppleEthernetRL` pattern in `stop`), the operationally
  safe value is 0.

## 6. Open points after this document

| # | Point | State |
|---|---|---|
| 4 | `ifnet` field consuming the tx data offset | **Closed**: `ifnet+0x58C` (u16; store `0xffffff800063fd48`); consumer `nx_netif_host_output` (`0xffffff80009ea796`, `0xffffff80009ea7b6`) and `fsw_ring_flush`/`sub_FFFFFF80009F86C0`. `?` declared name in xnu's `if_var.h`, absent from the SDK and MacKernelSDK: field fixed by offset and consumer |
| 5 | Binding of `r15` in `AppleBCMWLANSkywalkInterface::start` | **Closed**: `r15 = rdx` at `0xffffff80015d90ae` (2nd nominal parameter, `RegistrationInfo *` per the symbol); saved at `var_C8` (`0xffffff80015d947a`) and restored (`0xffffff80015d9518`) before the second read |
| 7 | Declared intent of the `uint` of `deregister*` | **Partial**: dead parameter in 25G241 (no reads in the two bodies; three callers with 0, `stop` and `free` with residue); by-reference destruction uses `terminate(2)`, not `deregister`. `?` declared intent, not deducible from the binary |
| — | xnu name of fields `ifnet+0x58C`/`+0x58E`/`+0x590` | `?` new (§1.3) |
| — | Consequence of not subtracting the headroom from the registered MTU in stage 1 | `?` new (§5) |

## 7. Address verification

Heads confirmed with `ida_funcs.get_func(ea).start_ea == ea`:
`0xffffff800063f3f0` (`_ifnet_allocate_extended`), `0xffffff8000640270` (`_ifnet_set_flags`),
`0xffffff8000641070` (`_ifnet_set_mtu`), `0xffffff8000641110` (`_ifnet_set_hdrlen`),
`0xffffff8000641130` (`_ifnet_hdrlen`), `0xffffff8000641090` (`_ifnet_mtu`),
`0xffffff8000641190` (`_ifnet_set_baudrate`), `0xffffff80005de1f0` (`_ifnet_set_promiscuous`),
`0xffffff80009a4140` (`kern_nexus_controller_alloc_net_provider_instance`),
`0xffffff80009faf20` (`nx_netif_default_llink_config`),
`0xffffff80009ea590` (`_nx_netif_host_output`), `0xffffff80009bb8a0` (`_fsw_ring_flush`),
`0xffffff80009f86c0` (`sub_FFFFFF80009F86C0`), `0xffffff8000bb2d3b` (`sub_FFFFFF8000BB2D3B`),
`0xffffff8002aff562` (`IOSkywalkNetworkBSDClient::start`),
`0xffffff8002b0066c` (`gatedPrepareNexus`),
`0xffffff8002b09e66` (`IOSkywalkNetworkInterface::stop`),
`0xffffff8002b09e9c` (`deregisterNetworkInterface`),
`0xffffff8002b09ffe` (`IOSkywalkNetworkInterface::free`),
`0xffffff8002b0a7d4` (`deregisterLogicalLink`),
`0xffffff8002b10a84` / `0xffffff8002b10af4` / `0xffffff8002b10b46`
(`registerEthernetInterface` ×2 and `deregisterEthernetInterface`),
`0xffffff8002b10bca` (`initBSDInterfaceParameters`),
`0xffffff80016aae4e` (`destroySkywalkInterface`),
`0xffffff80016aa874` (`createSkywalkInterface`),
`0xffffff80015d909a` (`AppleBCMWLANSkywalkInterface::start`),
`0xffffff80015d8dd4` (`AppleBCMWLANSkywalkInterface::start(IOService *)`),
`0xffffff80015e00bc` (`AppleBCMWLANLowLatencyInterface::start`).

Instruction citations inside a verified head (all addresses of §1–§3; the sites of
`0xffffff8002affa40`–`0xffffff8002affab7` and `0xffffff80015d909a`–`0xffffff80015d95c9` are inside
heads `0xffffff8002aff562` and `0xffffff80015d909a`; `0xffffff800063f626`–`0xffffff800063fd62`
inside `0xffffff800063f3f0`; `0xffffff80009ea740`–`0xffffff80009ea80c` inside
`0xffffff80009ea590`; `0xffffff80009bd035` inside `0xffffff80009bb8a0`; `0xffffff80009f870d`
inside `0xffffff80009f86c0`; `0xffffff80016aac2f`–`0xffffff80016aac48` inside
`0xffffff80016aa874`; `0xffffff80017296ff`–`0xffffff8001729752` inside the `start` of
`AppleBCMWLANProximityInterface` verified by `AWDL_STAGE1_BSDSTART_ORDER.md` §2.1).

Data read: cells `vptr+0x8E0`/`+0x8E8`/`+0x9C0` of `__ZTV25IOSkywalkNetworkInterface`,
`__ZTV26IOSkywalkEthernetInterface`, `__ZTV28AppleBCMWLANSkywalkInterface` and
`__ZTV30AppleBCMWLANProximityInterface`; cell `vptr+0x600` of the Skywalk/AppleBCMWLAN vtables;
cell `ZTV+0x14F0` of `__ZTV28AppleBCMWLANSkywalkInterface` (`0xffffff80017e3b18`) and of
`__ZTV31AppleBCMWLANLowLatencyInterface` (`0xffffff80017e5980`).
