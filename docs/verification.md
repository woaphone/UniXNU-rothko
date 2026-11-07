# Verification record

This file is the compact, repository-relative evidence index for `porting.toml`. Large pstore, dmesg, unixnu ring and firmware captures remain in the parent K70U bring-up workspace because they are proprietary or too large for this repository.

## Identity and host inputs

- Board: Xiaomi Redmi K70 Ultra (`rothko`), MediaTek MT6989 / Dimensity 9300.
- Donor: iPad7,1 / iPadOS 17.7.11 / build 21H461.
- XNU source: `git@github.com:woaphone/xnu-mtk.git`, stable branch `rothko`.
- XNU patch result: 18 ordered patches apply to immutable base
  `53bc420e5acda41c1689f6690a85841393a347cf` and produce
  `fd4edc747395525a914987bb88f063b5e549ecc7`.
- Kernelcache SHA-256: `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.
- Build route: official XNU link, XnuPrelink, UEFI `DeviceBuild.py`/`stuart`, payload and `boot_v4` packaging. The old Clang 18 compatibility route was used; no new LLVM toolchain is claimed.
- Repository record check: `python3 scripts/porting-record.py check --device ../UniXNU-rothko` reports 14 structured areas checked.

## Device observations

| Milestone | Result | Boundary |
| --- | --- | --- |
| XNU entry | K1-K4, MMU and serial initialization observed | Does not prove a complete userland boot by itself |
| CPUs | Eight-core startup, timer and IPI activity observed | Does not prove dynamic frequency scaling |
| Storage | UFS HS-G5 dual-lane/LU2 reads and root matching observed in the unx16 follow-up evidence | Does not prove persistent writes or current UEFI safety |
| Display | Activation page observed on the unx7 baseline with CMD active-push path | Long-term desktop stability and wake-after-lock remain open |
| Input | Goodix reports and HID queue movement observed | UI touch interaction and key wake remain unverified |
| Memory | Corrected 4 KiB bootstrap/page-table path and seed import reached later XNU stages | Long-duration pressure comparison is incomplete |
| Power | Fixed FDVFS boot request built into the offline candidate | No dynamic governor, battery service or sleep/resume proof |
| BROM event | Later UEFI candidate was followed by a BROM-only state | No evidence here proves LUN0, preloader or clock-frequency damage |

## How to reproduce the host side

From a checkout containing the main UniXNU tree and this repository:

```sh
./unixnu defconfig rothko_defconfig -D ../UniXNU-rothko
./unixnu fetch -D ../UniXNU-rothko
./unixnu status --json -D ../UniXNU-rothko
./unixnu tree -D ../UniXNU-rothko
python3 scripts/porting-record.py check --device ../UniXNU-rothko
../UniXNU-rothko/scripts/package.sh --no-deps --check --os 17 --uefi ../uefi-rothko
```

The main checkout must implement Format 2 and `release`; select it with
`UNIXNU_BASE` when necessary. Build inputs use `[build]` via `--config`;
device `local.toml` controls module sources. `--no-deps --check` inspects
configuration and patch paths without downloading or building. A successful
record/plan check does not qualify patch application, driver recipes or UEFI
build compatibility. The full release route is still blocked as recorded in
[`offline-audit-2026-11-07.md`](offline-audit-2026-11-07.md).

No device flashing, erase or recovery operation is part of this record.

## Candidate attribution

| Evidence | Source/candidate | Limit |
| --- | --- | --- |
| Activation UI | unx7, XNU `142fc32699a9faeb9e81fb93705319ec76ae0aaf`, vendor_boot `290d1de3a3350cc7274243c5318ddc6fd031f5e7d7d08ff8fd3593b5c001c8f0` | User observation; no stable desktop claim |
| Eight-core/HID activity | unx8, XNU `142fc32699a9faeb9e81fb93705319ec76ae0aaf`, vendor_boot `bee3d00e1ec6749ba4a4620fdb5788524e3db80f74c891de144f3568ab562fc2` | Startup/timer/IPI and report transport only |
| Root/UFS sustained reads | unx16, XNU `fa4d518c74d62f0f603df1fde1a12bc91515ecce`, vendor_boot `73514efc50e004b6289dc5452124d14acc753a3d8968f7b275be89a77eb0d84f` | Build16 capture reached 330 seconds and 5,573,267,456 bytes; UEFI NOP remained unresolved |
| FDVFS/Managed Preferences fixes | unx17, XNU `fd4edc747395525a914987bb88f063b5e549ecc7` | Offline build; later BROM report withdraws recommendation |
| UEFI refclk/write restrictions | final PHY-skip unx18, FD `7fedccdf2097080689526b0fcea14caa22c70d9dcf14af035dd841128c921bf9` | Offline build, partial CDB denylist and retained RTFF/HCI state changes; no runtime qualification |

Earlier record revision `d8def1f` was not the source revision of all these
device runs. The candidate-specific source/image identities above take
precedence. Local repository/configuration edits on 2026-11-07 do not change
or revalidate any delivered image.

## Evidence interpretation

`porting.toml` uses the official levels: `implemented` means source implementation with evidence, `host-verified` means a reproducible host command and revision, and `device-verified` means a device observation tied to the record. A device-verified area can still list narrower limits when only one milestone has been observed; completion criteria remain explicit in the same area.

