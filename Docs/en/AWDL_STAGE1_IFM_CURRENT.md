# `SIOCGIFMEDIA` on a role 6 virtual interface: where `ifm_current` comes from

Closes point 15.9 of `AWDL_STAGE1_IMPL_SPEC.md`: what value `SIOCGIFMEDIA` returns in
`ifmediareq+0x10` (`ifm_current`) for a role 6 `IO80211VirtualInterface`, and whether anything changes it
between the registration and the read of wifip2pd. Base: kernelcache of macOS 26.7.1 (25G241, Darwin 25.6.0, x86_64). All
addresses are VAs.

## 1. `ioctl_gifmedia` instruction by instruction

`IOSkywalkNetworkInterface::ioctl_gifmedia` (`0xffffff8002b0ae78`):

| VA | Instruction | Done |
|---|---|---|
| `0xffffff8002b0aea2` | `mov r12d, [rcx+20h]` | length of the user buffer |
| `0xffffff8002b0aeb2` | `mov rax, [rdi+0C0h]` | media descriptor of the interface |
| `0xffffff8002b0aeb9` | `mov rax, [rax]` | internal object of the descriptor |
| `0xffffff8002b0aec5` | `mov r13d, [rax+14h]` | media count |
| `0xffffff8002b0aee0` | `call _IOMallocZeroData_external` | temporary media table |
| `0xffffff8002b0af01` | `call qword ptr [rcx+948h]` | `getSupportedMediaArray(unsigned *, unsigned *)` fills the table |
| `0xffffff8002b0af99` | `call _copyout` | copies the table into the `ifm_ulist` of the user |
| `0xffffff8002b0afb1` | `mov [rbx+20h], r13d` | `ifm_count` |
| `0xffffff8002b0afb5` | `mov rax, [r14+0C0h]` | media descriptor |
| `0xffffff8002b0afbc` / `0xffffff8002b0afe2` | `lck_mtx_lock` / `lck_mtx_unlock` on `[rax+10h]` | read under the descriptor lock |
| `0xffffff8002b0afcc` → `0xffffff8002b0afcf` | `mov ecx, [rax+30h]` → `mov [rbx+1Ch], ecx` | `ifm_active` |
| `0xffffff8002b0afd2` → `0xffffff8002b0afd5` | `mov ecx, [rax+34h]` → `mov [rbx+10h], ecx` | **`ifm_current`** |
| `0xffffff8002b0afd8` → `0xffffff8002b0afdb` | `mov ecx, [rax+38h]` → `mov [rbx+18h], ecx` | `ifm_status` |

Answer to (1): the store into `ifmediareq+0x10` is fed by `*(*(this+0xC0)+0x34)`, a u32 of
the media descriptor of the interface, read under the descriptor lock. `ifm_active` comes from `+0x30`
and `ifm_status` from `+0x38` of the same object.

## 2. Writers of `+0x34`

Sweep of `mov [reg+34h], …` stores in the `IOSkywalk*` functions related to media and
registration. There are only two over the descriptor `*(this+0xC0)`:

| VA | Function | Done |
|---|---|---|
| `0xffffff8002b0a31e` → `0xffffff8002b0a32a` | `IOSkywalkNetworkInterface::registerNetworkInterface(RegistrationInfo const *, IOSkywalkLogicalLink *, …)` | `RegInfo+0x24` → descriptor `+0x30` (`ifm_active`) |
| `0xffffff8002b0a32d` → `0xffffff8002b0a332` | same | `RegInfo+0x28` → descriptor `+0x34` (`ifm_current`) |
| `0xffffff8002b0b184` | `IOSkywalkNetworkInterface::ioctl_sifmedia(__ifnet *, void *)` | only in response to an `SIOCSIFMEDIA` |

The copy happens during the registration of the interface, before `registerService`. Afterwards, the value
only changes if someone issues an `SIOCSIFMEDIA` over the interface. In IO80211Family there are no
writers of the field; the search for stores to `[reg+34h]` preceded by a load of `[x+0C0h]` in
`IO80211*` functions found none.

## 3. Value for stage 1

With the seed `RegInfo+0x28 = 0x80`, `SIOCGIFMEDIA` returns `ifm_current = 0x80` from the registration
until the first `SIOCSIFMEDIA`. `(0x80 & 0xE0) == 0x80` satisfies the filter of wifip2pd. With the Broadcom seed
`8` the filter would fail for this read (`8 & 0xE0 == 0`), which confirms deviation D5
of `AWDL_STAGE1_IMPL_SPEC.md`. `ifm_active` takes `RegInfo+0x24`, which stage 1 also fixes at `0x80`.

`?` It has not been checked whether wifip2pd, airportd or another process issues an `SIOCSIFMEDIA` over `awdl0`
before its read; test T5.1 of `AWDL_STAGE1_IMPL_SPEC.md` §14 (`SIOCGIFMEDIA` over `awdl0`)
resolves it on hardware.
