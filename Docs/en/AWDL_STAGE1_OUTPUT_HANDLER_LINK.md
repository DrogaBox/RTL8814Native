# Linking of the `ifnet` `output_handler`s on 26.7.1

Scope: the symbols `ifnet_set_output_handler`, `ifnet_reset_output_handler`,
`dlil_set_output_handler` and
`dlil_reset_output_handler`, which the stage 2 design used to bypass the `ifclassq` of `awdl0`.
Three questions are answered: whether they are exported to kexts, what alternative exists without a
symbol, and how to detect
their absence at load time. Everything was read over copies of the database and over the kernel
collection itself; nothing has been linked, loaded or run.

## 1. Answer

| Question | Answer |
|---|---|
| Are they exported to kexts on 26.7.1? | **No.** The four exist in the kernel symbol table as defined globals (type `0x0f`), but **none** appears in the nine KPI lists of `__LINKINFO,__symbolsets`; no kext of the collection imports or defines them, and they have no code references. |
| Which `OSBundleLibraries` entry provides them? | **None.** `com.apple.kpi.bsd`, `com.apple.kpi.private` and `com.apple.kpi.unsupported` do not contain them. Adding `private` would provide, among others, `_ifnet_allocate_extended` and `_ifnet_enqueue`, but not the four. |
| Can the kext install its own `output` without them? | **Yes**, through two virtual methods already declared in the header: `initBSDInterfaceParameters` (slot 284, call `[vptr+0x8E0]`) to set field `+0x40` of `ifnet_init_eparams`, or `prepareBSDInterface` (slot 285, call `[vptr+0x8E8]`) to write `ifp+0x258` with `cmpxchg`. Field `+0x38` does not work: the registration replaces it. |
| Can the absence be detected at load? | There is no exported API: `kmod_find_symbol` does not exist, and `_kext_request`, `_kmod_get_info` and `OSKext::lookupKextWithIdentifier` exist but are not in any KPI list. Only weak import with the fallback symbol `_gOSKextUnresolved` or the per-build gate `VersionGate.h` already uses remain. |

The immediate consequence is that step 1 of the stage 2 design (`ifnet_set_output_handler` in
`initWithController`) does not link on this version. The step must be replaced by its own write to
`ifp+0x258` or by the `+0x40` field of the parameters, both inside a virtual method the family
already invokes.

## 2. Export tables of the kernel collection

### 2.1 Which table decides the link

| Table | Location | Content | Does it decide a kext's link? |
|---|---|---|---|
| Kernel symbols | `com.apple.kernel`, `LC_SYMTAB` | 23,790 entries; defined externals have type `0x0f` | Not by itself |
| KPI lists | `__LINKINFO,__symbolsets`, VA `0xFFFFFF8003400000`, file `0x3300000`, `0x4D6D4` bytes | binary plist with `SymbolsSets` and `WeakRefFallbackSymbol` | **Yes**: it is the export table resolving `OSBundleLibraries` |
| Kext imports | `LC_SYMTAB` of each fileset | 88,653 `N_UNDF` entries; 9 weak | — |
| Kext definitions | `LC_SYMTAB` of each fileset | 204 filesets | — |

The kernel's `LC_SYMTAB` is not the table the loader uses for a kext: the KPI lists of the plist are
the ones carrying `CFBundleIdentifier` and the `Symbols` list of each collection. A symbol may be in
the kernel table and not be linkable from a kext, which is exactly the case with the four. In
this collection the 204 filesets carry `LC_SYMTAB` and none carries `LC_DYLD_INFO_ONLY` nor
`LC_DYLD_CHAINED_FIXUPS`: a kext import stays as an `N_UNDF` symbol in that table, which is where
it was searched, not in an export trie nor in a fixup chain.

### 2.2 The four symbols

| Symbol | VA | Type | Section | In any KPI list? | Imported by any kext? | Defined by any kext? |
|---|---|---|---|---|---|---|
| `_ifnet_set_output_handler` | `0xFFFFFF80005EF540` | `0x0f` | 1 | No | No | No |
| `_ifnet_reset_output_handler` | `0xFFFFFF80005EF560` | `0x0f` | 1 | No | No | No |
| `_dlil_set_output_handler` | `0xFFFFFF80005EF4E0` | `0x0f` | 1 | No | No | No |
| `_dlil_reset_output_handler` | `0xFFFFFF80005EF510` | `0x0f` | 1 | No | No | No |

The import sweep walks the 204 filesets and their 88,653 `N_UNDF` symbols; none of the four
appears. The definition sweep walks the `0x0f` entries of all filesets; only `com.apple.kernel`
defines them. The four functions also have no code references in the whole collection: not even
the kernel itself calls them.

### 2.3 The nine KPI lists

| List | Symbols | Any of the four? |
|---|---|---|
| `com.apple.kpi.bsd` | 876 | No |
| `com.apple.kpi.iokit` | 2,528 | No |
| `com.apple.kpi.libkern` | 1,010 | No |
| `com.apple.kpi.mach` | 77 | No |
| `com.apple.kpi.dsep` | 12 | No |
| `com.apple.kpi.unsupported` | 255 | No |
| `com.apple.kpi.private` | 1,739 | No |
| `com.apple.kpi.kasan` | 0 | No |
| `com.apple.kpi.kcov` | 0 | No |

The check was done over the exact names with the leading underscore, which is how the plist stores
them. There is no entry with `output_handler` in any of the nine lists.

### 2.4 Neighboring symbols that can be linked

| Symbol | List | Use on this path |
|---|---|---|
| `_ifnet_find_by_name` | `com.apple.kpi.bsd` | Getting the `ifnet` by name, if the hook pointer is not held. |
| `_ifnet_set_mtu`, `_ifnet_set_eflags` | `com.apple.kpi.bsd` | Used by the family itself in `prepareBSDInterface`. |
| `_bpfattach` | `com.apple.kpi.bsd` | `gatedPrepareNexus` calls it at `0x2B006CA`. |
| `_ifnet_allocate_extended`, `_ifnet_enqueue` | `com.apple.kpi.private` | Only if a custom `ifnet` were built; not the case. |
| `_gOSKextUnresolved` | `com.apple.kpi.libkern` | Fallback symbol for weak references. |

The `Info.plist` of the kext declares `com.apple.kpi.bsd`, `iokit`, `libkern` and `mach`; it does
not declare `private`.
None of those dependencies provides the four `output_handler`s.

## 3. Semantics of the four functions

| Function | Instructions | Effect |
|---|---|---|
| `ifnet_set_output_handler` (`0xFFFFFF80005EF540`) | `0x5EF544` `mov rax,[rdi+5A0h]`; `0x5EF54D` `lock cmpxchg [rdi+258h], rsi`; `0x5EF556` `setnz` | Installs the pointer only if `[ifp+0x258]` still holds the value saved in `[ifp+0x5A0]`; if not, returns `0x10`. |
| `ifnet_reset_output_handler` (`0xFFFFFF80005EF560`) | `0x5EF570`–`0x5EF57E`: reads `[ifp+0x258]`, loads `[ifp+0x5A0]` and repeats the `cmpxchg` | Restores the saved value; returns the previous one. |
| `dlil_set_output_handler` (`0xFFFFFF80005EF4E0`) | `0x5EF4E4` `lea rax,_dlil_output_handler`; `0x5EF4ED` `lock cmpxchg [rdi+248h], rsi` | Replaces `ifp+0x248` (the stack's `if_output` entry), not `ifp+0x258`; only if it still holds `_dlil_output_handler`. |
| `dlil_reset_output_handler` (`0xFFFFFF80005EF510`) | `0x5EF520`–`0x5EF527` | Restores `ifp+0x248` to `_dlil_output_handler`. |

The normal path of the stack is `_dlil_output_handler` (`0xFFFFFF80005EF500`): at `0x5EF504` it
loads
`[ifp+0x258]` and at `0x5EF50C` jumps to it. That is why `ifnet_set_output_handler` changes the
jump target without
touching the `if_output` of `+0x248`; `dlil_set_output_handler` changes the entry itself. Both
paths bypass the
`ifclassq`. The first is the one reproducible with a direct write to `ifp+0x258`.

The four symbols have no code reference in the collection: they are not used inside the kernel
itself and no KPI list exposes them on this version.

## 4. `ifnet_allocate_extended`: why `params+0x38` does not survive

| Instruction | Fact |
|---|---|
| `0xFFFFFF800063F49E`–`0x63F4A2` | Loads `[params+0x38]` into `[rbp+var_78]`. |
| `0xFFFFFF800063F4A6`–`0x63F4AA` | Loads `[params+0x40]` into `[rbp+var_A8]`. |
| `0xFFFFFF800063F4B1`–`0x63F4B5` | Loads `[params+0x48]` into `[rbp+var_60]`. |
| `0xFFFFFF800063F752` | `lea r15,_ifp_if_start`; the bytes `4c 8d 3d 87 45 fa ff` give `0xFFFFFF80005E3CE0`, not the address the disassembler shows on that line. |
| `0xFFFFFF800063F882` | `[ifp+0x258] = [params+0x38]`. |
| `0xFFFFFF800063F890` | `[ifp+0x260] = [params+0x40]`. |
| `0xFFFFFF800063F89D` | `[ifp+0x250] = r15` (`_ifp_if_start`). |
| `0xFFFFFF800063FC00` | `cmp qword [ifp+0x250],0`; if zero, it does not enter the block. |
| `0xFFFFFF800063FC0A` | `lock or dword [ifp+0xC0],0x80`: sets `IFEF_TXSTART`. |
| `0xFFFFFF800063FC16`–`0x63FC30` | `rax = [ifp+0x260]`; if zero, `rax = _ifnet_enqueue`; `[ifp+0x258] = rax`. |
| `0xFFFFFF800063FDA5` | `[ifp+0x5A0] = [ifp+0x258]`: saves the default value for the `reset`. |
| `0xFFFFFF800063FCF3` | `[ifp+0x598] = r14`: saves the default `if_start`. |
| `0xFFFFFF80009A41F4` | The nexus rejects the registration if `[params+0x48] != 0`. |
| `0xFFFFFF80009A420D` | The nexus calls `ifnet_allocate_extended` with those parameters. |

Since the nexus requires `params+0x48 == 0`, the registration installs `_ifp_if_start` at
`ifp+0x250`; the pointer is not
null, so `0x63FC00` enters the block, sets `IFEF_TXSTART` and replaces `ifp+0x258` with
`[ifp+0x260]` or `_ifnet_enqueue`. Field `+0x38`, which
`IOSkywalkNetworkBSDClient::start` fills with `IOSkywalkNetworkBSDClient::if_output`, does not
survive.

The value left in `ifp+0x258` with the current parameters is `_ifnet_enqueue`
(`0xFFFFFF80005E5ED0`),
because `params+0x40` is zero. `ifp+0x5A0` stores that same value and `ifp+0x598` stores
`_ifp_if_start`.
That is the state stage 2 would work on.

## 5. The virtual methods that can be used

### 5.1 `start` and `initBSDInterfaceParameters`

| Instruction | Fact |
|---|---|
| `0xFFFFFF8002AFFA18`–`0x2AFFA1B` | `bzero(params+8, 0x150)`: zeroes, among others, `+0x40` and `+0x48`. |
| `0xFFFFFF8002AFFA4D` | `[params+0x38] = IOSkywalkNetworkBSDClient::if_output` (`0x2B002CE`). |
| `0xFFFFFF8002AFFADC` | `call [vptr+0x8E0]`: `initBSDInterfaceParameters(params, &lladdr)`. |
| `0xFFFFFF8002AFFBB5` | `kern_nexus_controller_alloc_net_provider_instance(...)`, with `params` in the init block. |
| `0xFFFFFF8002B10BCA` | `IOSkywalkEthernetInterface::initBSDInterfaceParameters`: validates `version == 2` (`0x2B10BCF`) and `len == 0x158` (`0x2B10BE8`); if not, returns `0xE00002C2`. |
| `0xFFFFFF8002B10C15`–`0x2B10D0A` | Writes MAC (`+0x10`/`+0x18`), broadcast address (`+0x128`/`+0x130`), `ether_demux` (`+0xD8`), `ether_add_proto` (`+0xE0`), `ether_del_proto` (`+0xE8`), `ether_check_multi` (`+0xF0`), `ether_frameout` (`+0xF8`) and fields `+0x2C`/`+0x30`. It does not touch `+0x38` nor `+0x40`. |
| `0xFFFFFF80022F635C` | `IO80211SkywalkInterface::initBSDInterfaceParameters`: calls the base implementation (`0x22F6373`, `[vtable+0x8F0]`) and, if `[this+0x120]->[+0x58]` is 1 or 2 (`0x22F637F`–`0x22F638E`), sets `[params+0xB] |= 0x40` (`0x22F6390`). |

The base implementation does not write the two fields of interest; the IO80211 one neither. An
`override` in the
AWDL class can call the inherited implementation and then write field `+0x40` of
`ifnet_init_eparams`. With that value non-null, `ifnet_allocate_extended` copies it to `ifp+0x260`
and then to
`ifp+0x258` at `0x63FC30`, which is exactly the destination sought.

### 5.2 `prepareBSDInterface`

| Instruction | Fact |
|---|---|
| `0xFFFFFF8002B0066C` | `IOSkywalkNetworkBSDClient::gatedPrepareNexus(ifnet)`. |
| `0xFFFFFF8002B00688` | `call [vptr+0x8E8]` with `(ifnet, 0)`: `prepareBSDInterface`. |
| `0xFFFFFF8002B006A6` | Stores the `ifnet` at `[this+0xF8]`; the `bpfattach` comes after (`0x2B006CA`). |
| `0xFFFFFF8002331ADA` | `IO80211VirtualInterface::prepareBSDInterface`: calls the base (`0x2331AE4`), and if the bpf `attach` fails, logs the error. |
| `0xFFFFFF80022F63B6` | `IO80211SkywalkInterface::prepareBSDInterface`: BSD name, MTU 0x5DC for type 2, `ifnet_set_eflags`. |

The hook receives the already created `ifnet`, inside the provider registration and before the
interface is published.
It is the natural point to write `ifp+0x258` with its own value and save the previous one to
restore it in
`stop`. The call is virtual on the interface object, which is the same one registered in stage 1;
the header already declares the method virtual (`Source/IO80211/IO80211_25_6_0.h:289`, `:351` and
`:403`).

### 5.3 Design for stage 2

| Option | Where it installs | Advantage | Risk |
|---|---|---|---|
| A. `initBSDInterfaceParameters` | Field `+0x40` of the parameters, before the registration | The base validates `version`/`len`; if the version or size changes, it returns an error before writing | Depends on the `+0x40` displacement, on `if_start` staying non-null and on the layout not changing with the same version/size (`?`) |
| B. `prepareBSDInterface` | `ifp+0x258`, after the registration and before publishing | Direct access to the `ifnet`; does not depend on `ifnet_init_eparams` | Depends on `+0x258`/`+0x5A0`; no design validation |
| C. Strong import of the symbols | — | The documented way | Does not link on 26.7.1: not in KPI |
| D. Weak import | — | The kext loads even if absent | On 26.7.1 the pointer lands on the panic function, not the real one |

Option B reproduces what `ifnet_set_output_handler` does without calling it:

```
uintptr_t old = *(uintptr_t *)((char *)ifp + 0x5A0);
if (__sync_val_compare_and_swap((uintptr_t *)((char *)ifp + 0x258), old, (uintptr_t)rtlAwdOutput) != old)
        return 0x10;
```

and the restoration is `*(uintptr_t *)((char *)ifp + 0x258) = old`. The `cmpxchg` against
`[ifp+0x5A0]` is
the same the original function uses at `0x5EF54D`; if the field already changed, `0x10` is returned
and nothing is installed.

Option A leaves the same effect at registration: `[params+0x40] = rtlAwdOutput` and the `ifnet` is
born with that
`output`, without writing the `ifnet` by hand. It is preferable if the `ifnet` must already be
complete when the BSD client sees it; B is preferable to avoid depending on the layout of
`ifnet_init_eparams`.

## 6. Load-time detection

| Path | Does it exist on 26.7.1? | In any KPI list? | Verdict |
|---|---|---|---|
| `kmod_find_symbol` | Appears neither in the symbol table nor in the strings | — | Unusable |
| `_kmod_get_info` | Yes, `0xFFFFFF8000489850` | No | Unusable |
| `_kext_request` | Yes, `0xFFFFFF8000A516A0` | No | Unusable |
| `OSKext::lookupKextWithIdentifier` | Yes, `0xFFFFFF8000A39FB0` | No | Unusable |
| `_OSKextLookupSymbol` | No | — | Unusable |
| `_gOSKextUnresolved` | Yes, data at `0xFFFFFF800025CD78` | Yes, `com.apple.kpi.libkern` | Only as a fallback for weak references |

There is no exported function, among the `OSKext` and `kmod` APIs, letting a kext ask about
another symbol. `kext_request` is the request interface to the kext daemon, not a symbol searcher,
and it is not exported either.

The path that does exist is the weak import. The plist of `__LINKINFO,__symbolsets` declares
`WeakRefFallbackSymbol = _gOSKextUnresolved`; the data at `0xFFFFFF800025CD78` holds
`0xFFFFFF8000BB5F1A`, which is `_kext_weak_symbol_referenced`, and that function executes the
panic
«A kext referenced an unresolved weak symbol @%s:%d» (string at `0xFFFFFF8000CD26A5`). The
collection has
9 weak imports (`N_UNDF` with `n_desc & 0x40`), so the loader does know how to resolve them. A
weak reference to one of the four would not stay zero: per the plist fallback symbol, it would
point to the panic function, so the runtime check must compare the pointer against that
value before calling it (`?` whether the loader writes the address of the data or its value).

On 26.7.1 the four are not exported, so the weak import cannot be used to call them: the
gate has to choose the symbol-less path. For the Sonoma–26 versions the per-build gate is the one
already in `Source/AirLayer/VersionGate.h` (`version_major`, `osversion` and `OSMetaClass` sizes);
it
needs no symbol search. The weak import only makes sense as a safety net so the kext loads on a
build where the symbol is missing, not as a calling mechanism.

## 7. Risks and open questions

| Risk | Probability | Consequence | Mitigation |
|---|---|---|---|
| Displacement `ifp+0x258`/`ifp+0x5A0` different on another build | High | `ifnet` corruption | Per-build displacement table in the gate; only 26.7.1 is measured |
| Displacement `params+0x40` different on another build | High | The `output` is not installed or another field is written | The base validates `version`/`len`; still, per-build table |
| The nexus callback does not run before the first traffic | Low | The first packet takes the `ifclassq` | The hook runs inside the registration, before publishing; `?` the internal detail of `nx_create` |
| Another component replaces the `output` afterward | Low | The packet returns to the `ifclassq` | There are no references to the four symbols in the collection; `?` on future builds |
| The weak import binds to the data address and not to its value | Medium | The availability check fails | `?` the exact mechanism; do not use the weak import to call on 26.7.1 |
| The `awdl0` of an Apple Wi-Fi takes another path | Low | The design does not apply to that interface | The AWDL interface uses the same `IOSkywalkNetworkBSDClient::start`; `?` Apple's one |

## 8. Address verification

Checked heads (the cited address is the function start):
`0xFFFFFF80005EF4E0`, `0xFFFFFF80005EF510`, `0xFFFFFF80005EF540`, `0xFFFFFF80005EF560`,
`0xFFFFFF80005EF500`, `0xFFFFFF80005E3CE0`, `0xFFFFFF80005E5ED0`, `0xFFFFFF800063F3F0`,
`0xFFFFFF80009A4140`, `0xFFFFFF8002AFF562`, `0xFFFFFF8002B002CE`, `0xFFFFFF8002B10BCA`,
`0xFFFFFF80022F635C`, `0xFFFFFF8002331ADA`, `0xFFFFFF80022F63B6`, `0xFFFFFF8002B0066C`,
`0xFFFFFF8002B00340`, `0xFFFFFF8000BB5F1A`, `0xFFFFFF8000A516A0`, `0xFFFFFF8000A39FB0`,
`0xFFFFFF8000489850`.

Instruction citations inside a verified head: `0xFFFFFF80005EF544`, `0xFFFFFF80005EF54D`,
`0xFFFFFF80005EF556`, `0xFFFFFF80005EF570`–`0xFFFFFF80005EF57E`, `0xFFFFFF80005EF4E4`,
`0xFFFFFF80005EF4ED`, `0xFFFFFF80005EF520`–`0xFFFFFF80005EF527`, `0xFFFFFF80005EF504`,
`0xFFFFFF80005EF50C`, `0xFFFFFF800063F49E`–`0xFFFFFF800063F4B5`, `0xFFFFFF800063F752`,
`0xFFFFFF800063F882`, `0xFFFFFF800063F890`, `0xFFFFFF800063F89D`, `0xFFFFFF800063FC00`,
`0xFFFFFF800063FC0A`, `0xFFFFFF800063FC16`–`0xFFFFFF800063FC30`, `0xFFFFFF800063FDA5`,
`0xFFFFFF800063FCF3`, `0xFFFFFF80009A41F4`, `0xFFFFFF80009A420D`, `0xFFFFFF8002AFFA18`–`0x2AFFA1B`,
`0xFFFFFF8002AFFA4D`, `0xFFFFFF8002AFFADC`, `0xFFFFFF8002AFFBB5`, `0xFFFFFF8002B10BCF`,
`0xFFFFFF8002B10BE8`, `0xFFFFFF8002B10C15`–`0xFFFFFF8002B10D0A`, `0xFFFFFF80022F6373`,
`0xFFFFFF80022F637F`–`0xFFFFFF80022F638E`, `0xFFFFFF80022F6390`, `0xFFFFFF8002B00688`,
`0xFFFFFF8002B006A6`, `0xFFFFFF8002B006CA`, `0xFFFFFF8002331AE4`.

Data: `_gOSKextUnresolved` at `0xFFFFFF800025CD78` holds `0xFFFFFF8000BB5F1A`; the panic string is
at
`0xFFFFFF8000CD26A5`; the KPI lists plist is at `__LINKINFO,__symbolsets`, VA
`0xFFFFFF8003400000`, file `0x3300000`, `0x4D6D4` bytes.
