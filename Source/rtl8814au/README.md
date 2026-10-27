# Source/rtl8814au — vendor driver tree

This directory holds the upstream Linux driver that we are porting:

```bash
# from the repo root:
git submodule add https://github.com/morrownr/8814au Source/rtl8814au
# (or a plain clone if you prefer no submodules)
git clone --depth 1 https://github.com/morrownr/8814au Source/rtl8814au
```

License: GPL-2.0 (Realtek vendor driver, community-maintained).

## What gets ported vs replaced

| Vendor path | Fate in RTL8814Native |
|---|---|
| `core/*.c` (MLME, cmd, xmit, recv, security) | **Ported** (phases 2-3) |
| `hal/hal_com.c`, `hal/rtl8814a/**`, `hal/phydm/**` | **Ported** (phase 1) |
| `hal/rtl8814a/hal8814a_fw.c` | **Linked as-is** (embedded firmware) |
| `include/*.h` | **Used**, plus a new `osdep_service_macos.h` variant |
| `os_dep/linux/usb_intf.c` | **Replaced** by VendorGlue + RTL8814Native |
| `os_dep/linux/os_intfs.c` | **Replaced** by RTLEthernetAdapter (netdev lifecycle) |
| `os_dep/linux/usb_ops_linux.c` | **Replaced** by RTLUsbDevice (URB/pipe) |
| `os_dep/linux/recv_linux.c`, `xmit_linux.c` | **Replaced** by adapter glue + Compat skb |
| `os_dep/linux/ioctl_*.c`, `rtw_cfgvendor.c`, `rtw_proc.c`, `wifi_regd.c`, `rhashtable.c` | **Not ported** (UserClient replaces them) |
| `platform/` | Not ported |

Ported files are added incrementally to `VENDOR_SOURCES` in `build.sh`
as each phase demands — see `Docs/ROADMAP.md`.
