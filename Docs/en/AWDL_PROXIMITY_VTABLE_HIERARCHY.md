# Hierarchy AppleBCMWLANProximityInterface ↔ IO80211AWDLProtocol ↔ IO80211VirtualInterface

It closes the `?` left in `AWDL_KERNEL_ROLE_TABLE.md` §6: why `AppleBCMWLANProximityInterface::init` dispatches through the vtable of `IO80211AWDLProtocol` instead of its own vtable, and what the real inheritance relation is. Base: `BootKernelExtensions.kc` (md5 `f489ff8be80e6013a1bf47561dcaef45`), file reads + attached rea/IDA. Anything not seen goes as `?`.

**Short answer:**

1. **The inheritance is real and direct**: `AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` : `IO80211SkywalkInterface` : … The Broadcom class does not "reuse another's table for convenience": it is a C++/IOKit subclass of `IO80211AWDLProtocol` and its vtable (`0xffffff80017f5b68`) is its own and complete.
2. **The dispatch through the base's vtable is the codegen of a qualified call to the base's virtual member under the C++ ABI of kexts**: the compiler does not devirtualize it; it loads `&__ZTV19IO80211AWDLProtocol` (imported as an external symbol of the IO80211Family kext) from a `__data` slot of Broadcom and calls the slot of the 4-argument `init`, which holds the `init` inherited from `IO80211VirtualInterface` (because `IO80211AWDLProtocol` does not overwrite it).

## 1) Inheritance chain (evidence: metaclass in the initialization code)

There is no C++ RTTI to consult (`__ZTI`/`__ZTS`: 0 matches in `syms.txt` and `syms_bcm.txt`; kernel without RTTI). The IOKit hierarchy is registered in each `MetaClass`'s ctor, which passes the parent's `gMetaClass` to `OSMetaClass::OSMetaClass`. The three, decompiled with rea/IDA:

| class | function registering the metaclass | address | `superclass:` literal | `classSize` |
|---|---|---|---|---|
| `AppleBCMWLANProximityInterface` | `__GLOBAL__sub_I_AppleBCMWLANProximityInterface.cpp` (call to `OSMetaClass::OSMetaClass` at `0xffffff800172ad0e`) | `0xffffff800172ace5` | `&IO80211AWDLProtocol::gMetaClass` (`0xffffff800248cde8`, syms.txt:12226) | `0x140` |
| `IO80211AWDLProtocol` | `__ZN19IO80211AWDLProtocol9MetaClassC1Ev` | `0xffffff8002198bc8` | `&IO80211VirtualInterface::gMetaClass` (`0xffffff800248e478`, syms.txt:15631) | `0x130` |
| `IO80211VirtualInterface` | `__ZN23IO80211VirtualInterface9MetaClassC1Ev` | `0xffffff8002330002` | `&IO80211SkywalkInterface::gMetaClass` (`0xffffff800248dfa0`, syms.txt:15410) | `0x130` |

```
AppleBCMWLANProximityInterface (0x140)  ← com.apple.driver.AppleBCMWLANCoreMac
  └─ IO80211AWDLProtocol (0x130)        ← kext IO80211Family
       └─ IO80211VirtualInterface (0x130)
            └─ IO80211SkywalkInterface  (0x248dfa0)
                 └─ … (chain above SkywalkInterface not verified in this pass, ?)
```

Details:

- The sizes say `IO80211AWDLProtocol` **adds no ivars** (0x130 → 0x130): it is a pure protocol/interface class. `AppleBCMWLANProximityInterface` adds 0x10 of ivars.
- **The `gMetaClass` values read from the file are zero** (`0xffffff80017fd4e8` and `0xffffff800248cde8`: 5 qwords = 0): the instances are filled when the kext loads by the `__GLOBAL__sub_I_`; in the on-disk image the chain lives in the code above, not in the data. That is why the file-based walk attempt produces nothing and the evidence is the decompilation of the static-init/`MetaClassC1`.
- The vtable sizes corroborate simple inheritance without multiple inheritance (one single `__ZTV` per class, no secondary vtables): 506 → 569 → 572 slots (§3).

## 2) The "parent" call, instruction by instruction

`AppleBCMWLANProximityInterface::init(AppleBCMWLANCore*, ether_addr*, unsigned int, char*)` (`0xffffff8001718db0`, 0x684 bytes, Broadcom kext `com.apple.driver.AppleBCMWLANCoreMac`). rea/IDA listing:

```
0xffffff8001718dde:  mov rax, [rdi]                    ; this->vptr (vtable of ProximityInterface)
0xffffff8001718de1:  call qword ptr [rax+88h]          ; virtual this->init() 0-args
                                                       ;   slot +0x88 of its own vtable = initEv of Proxi (0x1718d72)
0xffffff8001718deb:  mov rax, cs:off_FFFFFF80017DE1C0  ; loads a pointer from Broadcom's __data
0xffffff8001718df2–dfe: rdi=rbx, rsi=r14, rdx=r13, ecx=r12d, r8=r15   ; the same 5 arguments
0xffffff8001718e01:  call qword ptr [rax+0E78h]        ; ← the call the pseudocode shows as off_FFFFFF80024572B8(...)
0xffffff8001718e07:  test al, al                       ; bool
0xffffff8001718e09:  jz loc_FFFFFF8001719185           ; error → r14d = 0
```

What lies behind `off_FFFFFF80017DE1C0`, read from the file:

- qword at `0xffffff80017de1c0` = `0x0040000002356440` → decodes to **`0xffffff8002456440` = `__ZTV19IO80211AWDLProtocol`** — the address of the vtable **symbol** (with header), not the vptr. The slot itself has no proper name (it falls inside a range `_.str.367+0x9e8`; IDA shows it as `off_`).
- `[ZTV + 0xE78]` = `0xffffff80024572b8`, which is `vptr(0x2456450) + 0xE68` — the slot of the **4-argument `init`** in the vtable of `IO80211AWDLProtocol`, whose content is `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (`0xffffff80023305f8`), verified in the diff of §3.
- The Broadcom kext **imports** the base class symbols as externals (undefs in `syms_bcm.txt`: `__ZTV19IO80211AWDLProtocol` :8568, `__ZN19IO80211AWDLProtocolC2EPK11OSMetaClass` :7537, `__ZN19IO80211AWDLProtocolD2Ev` :7538, `__ZN19IO80211AWDLProtocol10gMetaClassE` :7536) — standard pattern of a kext building subclasses of a class living in another kext.

**Why the base's vtable and not its own:** the call corresponds to the `init` of the base class (the dispatch points to the slot of `init(4 args)` using the vtable of `IO80211AWDLProtocol`, the immediate base). With Apple's kext C++ ABI (`-fapple-kext`), calls to virtual member functions are never devirtualized to direct `call`s — not even qualified ones — and a qualified call to the base is emitted as a load of the named class's vtable literal + `call [ztv + slot]`, which is exactly what is observed (attribution of the mechanism to the standard flag: reasonable inference; the observed codegen is fact). The contrast within the same function confirms it:

- **non-virtual** cross-kext call → direct `call`: `__ZN23IO80211SkywalkInterface14setInterfaceIdEj` at `0xffffff8001718e27`, `__ZN22AppleBCMWLANTaskmaster13withWorkQueueEP16IO80211WorkQueuej` at `0xffffff8001718ea4`;
- **virtual call on `this`** → via its own vptr: `call [this->vptr + 0x88]` at `0xffffff8001718de1`;
- **qualified virtual call to the base** (cross-kext) → via the base's imported ZTV: `call [rax+0xE78]` at `0xffffff8001718e01`.

The exact spelling in the source (`IO80211AWDLProtocol::init(...)` or another equivalent form) is not observable; what is observed is the dispatch through the base's vtable in the inherited init's slot.

## 3) Structure of the vtables (file read with fixups + syms names)

File mapping: `otool -l` per segment (the `vmaddr−fileoff` delta varies per segment: 0x200000 kernel, 0xFC000 in the region `0xffffff80017de000` of the Broadcom kext, 0x100000 in the IO80211Family region — the flat rule `fileoff = VA − 0xffffff8000100000` of the previous doc only holds within its segment). Fixups: `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)`, revalidated against `AirLink`. Headers of the three vtables: `[0x0, 0x0]` (offset-to-top 0, no typeinfo — consistent with no-RTTI).

| vtable | symbol | range | vptr | slots |
|---|---|---|---|---|
| `__ZTV23IO80211VirtualInterface` | `0xffffff8002472b78` | …`0xffffff8002473b58` | `0xffffff8002472b88` | 506 |
| `__ZTV19IO80211AWDLProtocol` | `0xffffff8002456440` | …`0xffffff8002457618` | `0xffffff8002456450` | 569 |
| `__ZTV30AppleBCMWLANProximityInterface` | `0xffffff80017f5b68` | …`0xffffff80017f6d58` | `0xffffff80017f5b78` | 572 |

The vptr `withOptions` saves (`0xffffff80017f5b78`, store at `0xffffff8001715a2a`) = symbol + 0x10 ✓.

**IO80211AWDLProtocol vs IO80211VirtualInterface** (506 common slots): only 3 real overrides, all identity — `D1` (+0x0), `D0` (+0x8), `getMetaClass` (+0x38) — and 4 differences that are the same function with a different fixup shape (rebase vs bind; verified by decoding the low32: +0x470 and +0xba8 point at `0xffffff8000bb6c80` and `getController` respectively in both). **63 new slots**, all in the tail (+0xFD0–0x11C0), all pointing to the same kernel stub `0xffffff8000bb59c0` (the first 40 verified one by one; the remaining 23 assume the same pattern, `?`; stub identity: unnamed in syms.txt, `?` — per the IOKit pattern, default/reserved bodies folded by ICF, inference). That is: **`IO80211AWDLProtocol` declares the AWDL virtual surface (apple80211 commands and reserved slots) with default bodies, and does not overwrite any behavior of `IO80211VirtualInterface`** — it matches there being no method symbol of `IO80211AWDLProtocol` in syms.txt beyond ctor/dtor/metaclass.

**AppleBCMWLANProximityInterface vs IO80211AWDLProtocol** (569 common slots): **95 overrides** — identity (D1/D0/getMetaClass), IOKit lifecycle (`init` 0-args +0x88, `free` +0x90, `start` +0x5C0, `stop` +0x5C8, `enable`/`disable` +0x850/+0x858, `setInterfaceEnable` +0x930, `reset` +0xEF8, …), Skywalk datapath (`getRxCompQueue`/`getTxCompQueue`/`getTxSubQueue`/`getTxPacketPool`/`enableDatapath`/`disableDatapath` +0xD70–0xDA0, `flushFlowQueues` +0xED8, `printDataPath` +0xC58, `getLogger` +0xD08, `isChipInterfaceReady` +0xB78, `setPromiscuousModeEnable` +0xA98, `getBSDNamePrefix` +0x9E8, `setMacAddress` +0xF40, `triggerAWDLMulticastTx` +0xF10, `p2pMulticastTx` +0xF18, `sendPendingPacketsToStack` +0xF30, `freePendingPackets` +0xF38) and the AWDL apple80211 command surface (+0xFC8–0x11C0: `getSSID`, `getCHANNEL`, `getBSSID`, `getSTATE`, `getAWDL_SYNC_PARAMS`, `getAWDL_SYNC_ENABLED`, `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS`, `getAWDL_ELECTION_ID`, `getAWDL_ELECTION_METRIC`, … — slots that in the vtable of `IO80211AWDLProtocol` were the stub). The remaining ~55 diffs follow the same pattern (not listed one by one, `?`). **3 own slots at the end**: +0x11C8 = `init(Core*, ether_addr*, unsigned, char*)` (`0x1718db0`), +0x11D0 = `freeResources` (`0x17194c8`, called on the error paths of `init` at `0xffffff800171917f`/`0xffffff80017192c5` as `call [vptr+0x11D0]`), +0x11D8 = NULL.

Key slots, the three vtables side by side:

| offset (vptr) | IO80211VirtualInterface | IO80211AWDLProtocol | AppleBCMWLANProximityInterface |
|---|---|---|---|
| +0x088 | `IO80211SkywalkInterface::init()` | same | `AppleBCMWLANProximityInterface::init()` (`0x1718d72`) |
| +0xE68 | `IO80211VirtualInterface::init(Controller*, ether_addr*, unsigned, char const*)` (`0x23305f8`) | same (inherited) | same (inherited) |
| +0x11C8 | — (vtable ends at +0xFC8) | — (ends at +0x11C8) | `AppleBCMWLANProximityInterface::init(Core*, ether_addr*, unsigned, char*)` (`0x1718db0`) |

The specialized `init` of Proxi (the one `withOptions` invokes through slot +0x11C8 with `a4 = 6`) is a **new virtual of the subclass** (signatures different from the 4-arg `init` of `IO80211VirtualInterface`: `AppleBCMWLANCore*` vs `IO80211Controller*`, `char*` vs `char const*`), which is why it does not step on slot +0xE68 and gets appended at the end. On the error paths of that init there is also `mov dword ptr [ivars+0x2B0], 6` (role 6 saved in ivars, `0xffffff8001718ec5`) and `setInterfaceId(2)` if the core version ≥ 5 (`0xffffff8001718e27`).

Context: the other three virtual interface vtables of the Broadcom kext have the same slot +0xE68 pointing to the `init` of `IO80211VirtualInterface` — `__ZTV33AppleBCMWLANIO80211APSTAInterface` (slot at `0x17f53e0`), `__ZTV24AppleBCMWLANNANInterface` (`0x17f7d00`), `__ZTV28AppleBCMWLANNANDataInterface` (`0x17f9128`) — consistent with roles 7 and 8–10 of the `setVIRTUAL_IF_CREATE` gate.

## 4) Answers

**Why does `AppleBCMWLANProximityInterface::init` dispatch through the vtable of `IO80211AWDLProtocol`?** Because it calls the virtual `init` of its base class, and kext codegen does not devirtualize qualified virtual calls: it emits `mov rax, cs:off_FFFFFF80017DE1C0` (a `__data` slot of Broadcom containing the imported pointer to `__ZTV19IO80211AWDLProtocol`) + `call [rax+0xE78]`. That slot of the base vtable holds the `init` inherited from `IO80211VirtualInterface` (`0x23305f8`) because `IO80211AWDLProtocol` does not overwrite it. Proxi's own vtable (`0x17f5b78`) does not take part in this call by ABI design: a qualified call to the base must resolve through the named class's vtable, not the object's. It is not casual table reuse: it is a real subclass calling its base across the kext boundary.

**What is the real inheritance relation?** `AppleBCMWLANProximityInterface` : `IO80211AWDLProtocol` : `IO80211VirtualInterface` : `IO80211SkywalkInterface` : (`?` above). Triple, independent evidence: (a) the three MetaClass records with the parent's `gMetaClass` as literal argument (§1); (b) the Broadcom kext's external imports of the class machinery of `IO80211AWDLProtocol` (§2); (c) the vtable structure 506 → 569 → 572 with simple inheritance, subclass overrides and 3 new slots at the end (§3).

## 5) Method and limits

- Vtables/strings/data: read from `BootKernelExtensions.kc` with the segment map of `otool -l` (delta per segment, not a flat rule) and fixup decoding `VA = 0xffffff8000100000 + (qword & 0xFFFFFFFF)` revalidated against `AirLink` on each pass.
- Metaclass chain: rea/IDA decompilation of `__GLOBAL__sub_I_AppleBCMWLANProximityInterface.cpp` (`0xffffff800172ace5`), `__ZN19IO80211AWDLProtocol9MetaClassC1Ev` (`0xffffff8002198bc8`), `__ZN23IO80211VirtualInterface9MetaClassC1Ev` (`0xffffff8002330002`) + instructions of `AppleBCMWLANProximityInterface::init` (`0xffffff8001718db0`).
- Names: `Docs/re/io80211_25.6.0/syms.txt` (IO80211Family/kernel) and `syms_bcm.txt` (Broadcom + externs).
- Explicit `?`: identity of stub `0xffffff8000bb59c0` (no symbol in syms.txt) and of the 23 final AWDL slots not printed; inheritance chain above `IO80211SkywalkInterface`; exact source spelling of the base call (only the codegen is observed); on-disk `gMetaClass` instances at zero (filled at load).
- Proxi's overrides were listed by sampling (95 in total); the ~55 not listed follow the documented pattern (`?` on individual detail).
