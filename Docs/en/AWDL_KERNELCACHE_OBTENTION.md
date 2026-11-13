# Obtaining the x86_64 kernel collection for the per-version reviews (macOS 14–26)

Practical guide to get the boot kernel collection of macOS 14, 15 and 26.x **on x86_64 and
without installing those versions**, open it in IDA and produce with the repo scripts the dumps
that `Tools/re/` and the version gate consume. The reference flow is the one of the kernelcache of
25.6.0 (macOS 26.7.1, build 25G241), dumped in `Docs/re/io80211_25.6.0/`.

Conventions:

- Everything without a `?` is confirmed by the repo sources or reproduced on the
  working machine (macOS 26.7.1, build 25G241, x86_64). The `?`s are steps and paths of installers or
  restore images this machine does not have present: verify when running them.
- This work requires x86_64. The kernelcaches of Apple silicon are arm64e and serve neither the
  reviews nor the dumps.
- After any macOS update the dumps expire: vtable slots move between
  builds (`PHASE5_IO80211.md`).

## 1. Which file to get

| File (installed system) | Role | On 25.6.0 |
|---|---|---|
| `/System/Library/KernelCollections/BootKernelExtensions.kc` | Boot collection: `com.apple.kernel` + the Apple kexts. Here live `IO80211Family`, `IOSkywalkFamily`, `IONetworkingFamily`, `corecapture`, `AppleBCMWLANCoreMac` | 67,584,000 B, md5 `f489ff8be80e6013a1bf47561dcaef45`, sha256 `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc` |
| `/System/Library/KernelCollections/SystemKernelExtensions.kc` | System collection: `WLANDriver` and the rest of auxiliary kexts | 369 MB; the current AWDL reviews do not need it |

Confirmed on the working machine (26.7.1):

- Both files exist at that path and are readable without `sudo` (`-rwxr-xr-x root wheel`).
- The `.kext` directories of `/System/Library/Extensions` only carry an `Info.plist`: the code is
  in the collections (`PHASE5_IO80211.md`).
- `BootKernelExtensions.kc` is a flat fileset Mach-O, without an IMG4 layer: the repo scripts
  read it directly.

Target versions and their Darwin majors (the gate compares the major: `GateLogic.h`):

| macOS | Darwin | builds | Intel installer |
|---|---|---|---|
| 14 (Sonoma) | 23 | 23Xxx | yes: 14.8.9 (23J631), §2.4 |
| 15 (Sequoia) | 24 | 24Xxx | yes: 15.8.2 (24H232), §2.4 |
| 26 (Tahoe) | 25 | 25Xxx | yes: 26.7.2 (25G332), §2.4; this machine runs 26.7.1 x86_64, build 25G241 |

Intel distribution versus Apple silicon: on Intel the boot collection is the x86_64
fileset of `KernelCollections` (what this repo uses). On Apple silicon the same paths exist on the
sealed system volume (?) but contain arm64e, and the restore IPSWs additionally carry an
arm64 `kernelcache.release.*` (?): neither of the two forms serves this work.

## 2. Sources without installing the version

### 2.1 Copy from a system already running the version (route confirmed for 25.6.0)

It is the route that produced the reference artifact: the IDB of
`Build/ida_awdl/BootKernelExtensions.i64` was created from the system file of a machine with
25G241 and the md5 matches the one of `/System/Library/KernelCollections/BootKernelExtensions.kc`
(`AWDL_BKE_ATTACH.md` §1). It suffices to copy from any Mac running the target version:

```
md5 /System/Library/KernelCollections/BootKernelExtensions.kc
cp /System/Library/KernelCollections/BootKernelExtensions.kc ~/re_cache/ida/boot_<build>.kc
```

The working copy of 25.6.0 lives at `~/re_cache/ida/boot.kc`; IDA is only used over copies
(rule of the reverse engineering session).

### 2.2 "Install macOS" app (route not verified on this machine: no installer present)

1. Download the full installer of the target version. The flags exist on this machine:

   ```
   softwareupdate --list-full-installers
   softwareupdate --fetch-full-installer --full-installer-version 14.7.1
   ```

   Which old versions the Apple catalog still offers: `?`. Alternatives: the
   direct-download `InstallAssistant.pkg` (?) or the mist CLI (?) — none installed here.
2. The app carries the content in `Contents/SharedSupport/SharedSupport.dmg` (?).
3. Mount read-only (flag `-readonly` confirmed in the help of `hdiutil attach`):

   ```
   hdiutil attach -readonly "/Applications/Install macOS Sonoma.app/Contents/SharedSupport/SharedSupport.dmg"
   ```

4. The mounted volume contains the full package `InstallAssistant.pkg` and the recovery
   directory `com.apple.recovery.boot/` (?).
5. Expand the package and locate the collection inside the payload:

   ```
   pkgutil --expand InstallAssistant.pkg ./ia
   ```

   `--expand` is documented in the help of this machine; `--expand-full` (which additionally extracts the
   payload) does not appear in that help: `?`. Inside the expanded payload, the collection should be
   at `System/Library/KernelCollections/BootKernelExtensions.kc` (?) and the build at
   `System/Library/CoreServices/SystemVersion.plist` (?). If `--expand` leaves the `Payload`
   unextracted, pbzx/cpio must be decompressed by hand (?).
6. The recovery image (`BaseSystem`) carries a reduced collection (?): it does not serve the
   reviews; use the one from the full installation payload.

### 2.3 Restore images (IPSW / OTA)

- Apple silicon IPSWs: arm64e `kernelcache.release.*`. Out of scope.
- Restore IPSWs of Intel Macs with a T2 chip: they exist per model (?) and should carry an
  x86_64 kernelcache (?); they are downloaded with the `ipsw` CLI (?). `ipsw` and `pyimg4` are not
  installed on this machine (?).
- OTA updates of Intel via `ipsw download ota` (?).
- Only this route needs unpacking IMG4/im4m (`pyimg4` or equivalent, ?): the KC of an
  installed system or of an installer is a flat fileset Mach-O.

### 2.4 `InstallAssistant.pkg` from the updates catalog (route confirmed for 14, 15 and 26)

Reproduced on 2026-10-30 on the working machine for 14.8.9, 15.8.2 and 26.7.2, without installing
any of them and without `sudo`:

1. The public catalog
   `https://swscan.apple.com/content/catalogs/others/index-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog.gz`
   lists each full installer as a product with an `InstallAssistant.pkg` on
   `swcdn.apple.com`. The `.dist` of each product gives version and build. Downloaded sizes:
   13,654,790,295 B (14.8.9), 15,664,531,445 B (15.8.2) and 18,389,893,051 B (26.7.2).
2. `xar -xf InstallAssistant.pkg SharedSupport.dmg` extracts the image without expanding the rest of the
   package. It is mounted with `hdiutil attach -readonly -nobrowse`.
3. The image contains an asset zip (over 1 GB) and the system goes in its chunks
   `AssetData/payloadv2/payload.NNN`, in Apple Archive format. The zip does not carry the loose
   collection: in `AssetData/boot/` there are only the `BootKernelExtensions.kc.*.im4m` manifests and, on 14.8.9,
   a 27,835,689 B `kernelcache.release.mac15j` that this flow does not use.
4. `aa` does not read from standard input, so each chunk is dumped to a temporary file with
   `unzip -p` and listed with `aa list -i`. The chunk that contains
   `System/Library/KernelCollections/BootKernelExtensions.kc` is extracted with
   `aa extract -i <chunk> -d extracted -include-path <path>`. `SystemVersion.plist` is searched the same way.
   The scripts used are at `~/re_cache/kc/extract_kc.sh` and `find_kc_chunk.sh`.
5. The result is the same flat fileset Mach-O as on an installed system. IDA opens it whole.

| Build | macOS | `BootKernelExtensions.kc` | md5 | sha256 |
|---|---|---|---|---|
| 23J631 | 14.8.9 | 62,783,488 B | `2ab1c3ac3db747642bf4cd6ac73e12f5` | `3f5dcde57f2d0ce27b9c9abf3d1e3d2c36b33cea9ae0a7eb36bf9c4282ed8cd2` |
| 24H232 | 15.8.2 | 66,125,824 B | `bcf4c23aa5f52f69951aea31c044f5de` | `ddd9016e225a61ebfd4f04b02a00937262f26690c57a928613e08506978c81be` |
| 25G332 | 26.7.2 | 67,600,384 B | `b9da5eafcd90e439ae7c55c1ca8ddf33` | `96c12f421a8e99580b5015dfd39877df38ecde9b9e056b8ccd28f15847bca35c` |

The build comes from the `ProductBuildVersion` of the `SystemVersion.plist` extracted from the same payload.

## 3. Artifact identity

Before dumping anything, record the build, size, md5 and sha256 of the copy:

```
ls -l <copy.kc>
md5 <copy.kc>
shasum -a 256 <copy.kc>
```

The build is read with `sw_vers -buildVersion` on the origin machine or, in an installer payload, from its
`SystemVersion.plist` (§2.4); both routes are confirmed. It remains to verify that the KC of an
installer matches the one of an installed system of the same build `?`. It is checked by comparing the
md5 of 25G332 of the table of §2.4 with the one of `/System/Library/KernelCollections/` after
updating this machine to 26.7.2. Example record row (the one the
HANDOFF uses for 25.6.0):

| Build | macOS | File | Size | md5 |
|---|---|---|---|---|
| 25G241 | 26.7.1 | `BootKernelExtensions.kc` | 67,584,000 B | `f489ff8be80e6013a1bf47561dcaef45` |

## 4. Opening the collection in IDA (without splitting it)

The repo flow does not split the KC: IDA 9.x opens it whole as a Mach-O fileset (kernel base
`0xffffff8000200000`, module size `0x3fe506c`; `AWDL_BKE_ATTACH.md`). Operational rule: IDA
headless only over a copy of the IDB or of the KC, never over the original.

The fileset mechanics, in case a tool requires one Mach-O per kext: each
`LC_FILESET_ENTRY` (cmd `0x80000035`) carries the vmaddr, the file offset and the name of the entry, and
the load commands of that entry start at its file offset; inside each entry,
`LC_SEGMENT_64` (cmd `0x19`) gives the vm/vs and fo/fs of each segment. Enumeration verified over the KC
of this machine (the same walk `Tools/re/kcsyms.py` does):

```python
import struct, sys
kc = open(sys.argv[1], 'rb').read()
def lcs(off):
    ncmds = struct.unpack_from('<I', kc, off + 16)[0]; p = off + 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from('<II', kc, p); yield cmd, p, cs; p += cs
for cmd, p, cs in lcs(0):
    if cmd == 0x80000035:
        vm, fo, eo = struct.unpack_from('<QQI', kc, p + 8)
        name = kc[p + eo:p + cs].split(b'\0')[0].decode()
        print(name, hex(vm), hex(fo))
```

The physical cut of an entry would be `kc[fo : fo+fs]` of its segments (?), and external
split tools (?) are not installed here. It is not needed for anything in the repo flow: each
entry carries its own symbol table and the scripts read it in place.

## 5. Symbol dumps (repo format)

`Tools/re/kcsyms.py <kc> <bundle-id>` dumps the segments (to stderr) and the symtab of one entry
of the fileset. The six dumps of 25.6.0 are reproduced byte for byte with these commands (verified
by md5 on this machine):

```
mkdir -p Docs/re/io80211_<darwin>.<delta> && cd Docs/re/io80211_<darwin>.<delta>
KC=/path/to/BootKernelExtensions.kc
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.kernel                        > syms_com.apple.kernel.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IONetworkingFamily      > syms_com.apple.iokit.IONetworkingFamily.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IOSkywalkFamily         > syms_com.apple.iokit.IOSkywalkFamily.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.iokit.IO80211Family           > syms.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.driver.corecapture            > syms_com.apple.driver.corecapture.txt
python3 ../../../Tools/re/kcsyms.py "$KC" com.apple.driver.AppleBCMWLANCoreMac    > syms_bcm.txt
```

Line format: `<VA hex 16> <nlist type> <name>`. Observed types: `0f` defined external
symbol, `0e` defined local, `01` undefined external. A line with VA `0000000000000000` is an
undefined reference of that module, not a definition (`Tools/awdl_recheck_symbols.py`). Verify
completeness: kcsyms.py announces `SYMTAB <n>` on stderr and the dump must have `n` lines (on
25.6.0, `syms.txt` has exactly 20001).

Consumption of the dumps:

| Tool | Dumps it asks for |
|---|---|
| `Tools/awdl_recheck_symbols.py` | the five `syms*.txt` of the system + family (`kernel`, `IONetworkingFamily`, `IOSkywalkFamily`, `corecapture`, `syms.txt`); with the five, 25.6.0 gives 128/128 OK |
| `Tools/re/check_io80211.sh <dump_dir>` | glob `syms*.txt` of the directory, excludes type `01` |
| `Tools/re/vt.py` | all the `syms_*.txt` of the working directory |

The 25.6.0 set additionally includes `classes.txt` (list of class names, origin not
documented in the scripts: ?).

## 6. Vtable dumps

`Tools/re/vt.py <Class>` runs from the dump directory (it needs the `syms_*.txt` there
and `c++filt` in the PATH) and writes `vt_<Class>.txt` with one line per cell:
`<index> 0x<offset> <demangled name>`.

- The KC pointers use chained fixups: the low 30 bits are the displacement of the target
  from the lowest vmaddr of the KC (`0xffffff8000100000`, `__HIB`), not from `__TEXT`
  (`0x...200000`) — `PHASE5_IO80211.md`. That base is a 25.6.0 constant inside `vt.py`.
  `otool -l` gives the same pair `__HIB 0xffffff8000100000` / `__TEXT 0xffffff8000200000` on the KCs
  of 23J631, 24H232, 25G241 and 25G332, so the constant holds for 14, 15 and 26.
- The KC path is fixed inside `vt.py` (`/System/Library/KernelCollections/
  BootKernelExtensions.kc`): as-is it only analyzes the KC of the running system; for a copy of
  another version that constant would have to be pointed at the copy (code edit: pending OK).
- The vtable set of 25.6.0: the 7-class chain of the gate (`IOSkywalkInterface`,
  `IOSkywalkNetworkInterface`, `IOSkywalkEthernetInterface`, `IO80211SkywalkInterface`,
  `IO80211InfraInterface`, `IO80211InfraProtocol`, `IO80211Controller`), their SDK parents
  (`IOService`, `IOEthernetController`) and the Apple implementations that name the pure
  slots (`AppleBCMWLANCore`, `AppleBCMWLANInfraProtocol`, `AppleBCMWLANSkywalkInterface`).

## 7. Header and version gate

Full chain for a new version (what is not marked is confirmed in the scripts):

1. Symbol dumps (§5) and vtable dumps (§6) in `Docs/re/io80211_<darwin>.<delta>/`.
2. Slot-exact header: `python3 Tools/re/gen_io80211.py <dump_dir> Source/IO80211/IO80211_<build>.h`.
   The instance sizes (`SIZES`) are taken from the `OSMetaClass(name, super, size)` of the KC and in the
   script those of 25.6.0 are at hand: for another version they must be measured again in IDA and
   updated (code edit: pending OK).
3. Static verification: `Tools/re/check_io80211.sh <dump_dir>` (vtables of a stub against Apple's,
   imports against the dumps) and
   `python3 Tools/awdl_recheck_symbols.py <the five dumps>`.
4. Gate: `python3 Tools/airlayer_gate.py gen` regenerates `Source/AirLayer/KnownBuilds.inc`
   (not edited by hand) from the header and `Tools/airlayer_builds.txt`. The `HEADER` constant of the
   script points today to `IO80211_25_6_0.h`: another version requires its header (code edit:
   pending OK).
5. Validation on hardware: the gate compares at runtime the Darwin major and, per class, existence,
   instance size and superclass (`GateLogic.h`), and additionally requires the build to be in the
   validated list. A new build is tested with boot-arg `rtlairtrust=<build>` (`airlayer_gate.py
   status` prints it) and only after passing the tests it is added with
   `Tools/airlayer_gate.py learn --yes`, which writes the line `build <build> <version> validated
   <date>` into `Tools/airlayer_builds.txt` and regenerates the `.inc`.

The md5 of the KC does not enter the gate: it is traceability of the artifact that produced the dumps (like
the row of 25G241 in `HANDOFF_AWDL.md`). Record it next to the dump directory.

## 8. Recommended record per version

```
Docs/re/io80211_<darwin>.<delta>/
  syms.txt                                   com.apple.iokit.IO80211Family
  syms_com.apple.kernel.txt                  com.apple.kernel
  syms_com.apple.iokit.IOSkywalkFamily.txt   com.apple.iokit.IOSkywalkFamily
  syms_com.apple.iokit.IONetworkingFamily.txt com.apple.iokit.IONetworkingFamily
  syms_com.apple.driver.corecapture.txt      com.apple.driver.corecapture
  syms_bcm.txt                               com.apple.driver.AppleBCMWLANCoreMac
  vt_<Class>.txt                             one per class of §6
  KC.md5 / KC.sha256                         identity of the origin artifact
```

Checklist per version: KC copy with build/md5/sha256 recorded → six dumps with the `SYMTAB`
count squared → `vt_*.txt` → header + `check_io80211.sh` + `awdl_recheck_symbols.py` →
only after testing on hardware, `learn --yes`.
