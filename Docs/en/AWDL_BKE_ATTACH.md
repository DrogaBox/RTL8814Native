# rea/IDA attach to BootKernelExtensions.kc (macOS 26 kernelcache)

Log of the re-attach session of the rea/IDA adapter (`attached` mode) to the base that the IDA GUI
has open since the 2026-10-28 change: `Build/ida_awdl/BootKernelExtensions.i64` (737 MB), created
from the macOS 26 kernelcache. Read-only; no `.i64` was opened by path. Anything not seen is
recorded as `?`.

## 1) Module identity (metadata reported by the upstream on every call)

| Field | Value |
|---|---|
| path | `/System/Library/KernelCollections/BootKernelExtensions.kc` |
| module | `BootKernelExtensions.kc` |
| base | `0xffffff8000200000` |
| size | `0x3fe506c` (~67 MB) |
| filesize | `0x4074000` |
| md5 | `f489ff8be80e6013a1bf47561dcaef45` |
| sha256 | `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc` |
| crc32 | `0xd2ee2fed` |
| upstream | ida-pro-mcp 1.30.0, adapter rea ida v1, mode `attached`, `cache_policy: live` |

The md5 matches exactly the one of the system file `/System/Library/KernelCollections/BootKernelExtensions.kc` (67,584,000 B, date 24-Sep): **the IDB was created from that kernelcache** and the original file exists, unlike the wifip2pd case.

## 2) Problem found and re-attach resolution

After changing the base in the GUI, the REA session was still bound to `wifip2pd.x86_64`
(`binary_session` reported `open: true` with the old path). With that stale binding, every
operation against the upstream failed:

- `list_documents` → error `unreadable_output` / `execution_failure` "Analysis returned an unreadable result. Run `rea doctor`…" (without `diagnostics.reason`).
- `procedure_address("0xffffff80022886a8")` and `("0xffffff80016788f4")` → same `unreadable_output` error.

**Resolution:** `open_binary(path="/System/Library/KernelCollections/BootKernelExtensions.kc", provider_id="ida")`. The adapter validated the identity against the base open in the GUI (it matches) and re-bound the session; from then on every result carries `metadata_before/metadata_after` with `module: BootKernelExtensions.kc`, which allows verifying per call which base answered. No new base was opened, and nothing headless.

## 3) Verified anchors (addresses from `AWDL_ROLE6_RE.md`, now with symbol and size)

| VA | Symbol (real name from the IDB) | Size | Evidence |
|---|---|---:|---|
| `0xffffff80022886a8` | `__ZL20setVIRTUAL_IF_CREATEP17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req` | `0xbe` | `procedure_address` → `get_function_by_address/name`, doc `AWDL_ROLE6_RE.md` §1 |
| `0xffffff80016788f4` | `__ZN16AppleBCMWLANCore20setVIRTUAL_IF_CREATEEP30apple80211_virt_if_create_data` | `0x6d2` | `procedure_address`, doc `AWDL_ROLE6_RE.md` §2 |

Notes on the observed signatures (macOS 26): the static IO80211Family `setVIRTUAL_IF_CREATE` receives `IO80211Controller*, IO80211SkywalkInterface*, IO80211APIUserClient*, apple80211req*` (the previous doc spoke of length 32 and data pointer in `apple80211req`; the IDB signature adds the explicit Skywalk interface — only what the symbol shows, without interpreting). The `AppleBCMWLANCore` override receives `apple80211_virt_if_create_data*` — consistent with the documented reading of the role as a dword at `request+12`; the function body was not reviewed in this pass (`?`).

## 4) Operational limitations observed on this base

- `binary_overview` **does not answer within the 30 s timeout** (full inventory of functions/strings over the 737 MB IDB). Exact count of procedures/strings: `?`. Use targeted queries (`procedure_address`, `xrefs`, `procedure_callees/callers`, `procedure_pseudo_code`) instead of the overview.
- `read_bytes` is still not declared by the ida provider (same limitation as with wifip2pd); for raw bytes, map VA→file offset with `otool -l` on the `.kc` (67 MB, viable).
- `procedure_info` is not declared by this provider (error `capability_unavailable`, "operation is not declared by this provider"); use `procedure_address` for the canonical entry and size.
- `search_procedures`/`search_strings` do not filter (behavior already known from the adapter); resolve by exact name with `procedure_address` or extract strings from the `.kc` file directly.
- Kernel-space addresses belong to the base segment `0xffffff8000200000`; for file reads, the VA→file offset mapping of this kernelcache has not been computed yet (`?`).

## 5) Status

The REA session is re-bound to `BootKernelExtensions.kc` and operational (2/2 anchors resolved with symbol). To go back to wifip2pd when the GUI changes base: same procedure with `open_binary("…/Build/ida_awdl/wifip2pd.x86_64", provider_id="ida")`.
