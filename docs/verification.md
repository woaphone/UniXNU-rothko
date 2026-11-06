# Verification record

This file is the compact, repository-relative evidence index for `porting.toml`. Large pstore, dmesg, unixnu ring and firmware captures remain in the parent K70U bring-up workspace because they are proprietary or too large for this repository.

## Identity and host inputs

- Board: Xiaomi Redmi K70 Ultra (`rothko`), MediaTek MT6989 / Dimensity 9300.
- Donor: iPad7,1 / iPadOS 17.7.11 / build 21H461.
- XNU patch result: 18 ordered patches apply to `origin/rothko` and produce `fd4edc747395525a914987bb88f063b5e549ecc7`.
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
scripts/package.sh --check --os 17 --uefi ../uefi-rothko
```

The package command needs the `ChallengeThePeak/Silicium-Rothko` checkout and the local IPSW/firmware inputs. No device flashing, erase or recovery operation is part of this repository record.

## Evidence interpretation

`porting.toml` uses the official levels: `implemented` means source implementation with evidence, `host-verified` means a reproducible host command and revision, and `device-verified` means a device observation tied to the record. A device-verified area can still list narrower limits when only one milestone has been observed; completion criteria remain explicit in the same area.

