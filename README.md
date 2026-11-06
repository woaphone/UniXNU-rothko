# UniXNU-rothko

Device repository for the Xiaomi Redmi K70 Ultra (`rothko`, MediaTek MT6989 / Dimensity 9300).
It follows UniXNU's [Port a device](https://unixnu.com/porting/) and [device repository](https://unixnu.com/porting/device-repository) format 2. The public repository contains the board contract, XNU patch series, board drivers, package kit and verification record. UEFI remains in the private `challengethepeek/uefi-rothko` repository and is selected through `board.toml`.

The port targets the iPad7,1 / iPadOS 17.7.11 / build 21H461 donor used by the established UniXNU sheng/yunluo ports. The board inventory records eight CPUs, a 13 MHz timer, MT6989 GICv3, UFS4 storage, a 1220x2712 panel and Goodix Berlin9916K touch hardware.

## Repository map

- `board.toml` — format 2 board identity, IPSW hashes, GPT/userdata windows, XNU/prelink, private UEFI contract, payload and ADT inputs.
- `feeds.toml` — current device-local MT6989 source and UFS source pins. A reusable public MT6989 common repository is still pending.
- `porting.toml` — official schema version 1 record with configuration, firmware, CPU, memory, storage, display, input, GPU, network, cellular, audio, power, packaging and diagnostics areas.
- `configs/rothko_defconfig` and `patches/xnu/series` — selected kernel configuration and ordered XNU patches.
- `dts/rothko.dts` — reviewable board hardware description used by the ADT generator.
- `drivers/` and `kits/rothko/` — board and device implementations, including MT6989 platform/CPU, readonly UFS/Data, display, HID, Goodix and fixed FDVFS request code.
- `package/` and `scripts/package.sh` — board package and the official `unixnu release` wrapper.
- `docs/verification.md` — compact evidence boundary and reproducibility record.

Large IPSW, firmware, logs, images and generated output stay outside Git. Their hashes and locations belong in the parent K70U bring-up reports or the local build record.

## Porting workflow

The repository follows the eight official stages:

1. **Collect inputs.** Hardware addresses, partition geometry, panel/touch resources, CPU topology and firmware facts are recorded in `dts/rothko.dts`, `board.toml` and `porting.toml`.
2. **Choose shared support.** The iPadOS profile, ARM64/XNU patch base and reusable catalog components are selected in `board.toml`; board-local MT6989 sources remain explicit in `feeds.toml`.
3. **Create the device repository.** The format 2 files, device kit, package, patch series, workflows and local override example are tracked here. `example = false` is set.
4. **Resolve and inspect.** Run the commands below from the main UniXNU checkout and review source revisions, overrides and patch order.
5. **Prepare hardware and firmware.** The DTS feeds the XNU ADT. The private UEFI repository supplies DarwinBoot, GOP, FAT initrd support and the boot FD.
6. **Adapt drivers in dependency order.** Platform/memory, storage/root, display, input, GPU, network, audio and power each have an area in `porting.toml`; unfinished areas stay `planned`.
7. **Build and start.** Build kernel, prelink, firmware, payload and boot image through the official tools. Device observations are recorded separately from host results.
8. **Complete and publish.** Run the record checker, review the generated diff, publish the repository and add the `unixnu` GitHub topic. The private UEFI checkout is required for a complete package build.

## Resolve and inspect

From the main UniXNU checkout:

```sh
./unixnu defconfig rothko_defconfig -D ../UniXNU-rothko
./unixnu fetch -D ../UniXNU-rothko
./unixnu status --json -D ../UniXNU-rothko
./unixnu tree -D ../UniXNU-rothko
python3 scripts/porting-record.py check --device ../UniXNU-rothko
```

The official checker currently reports 14 structured areas, including the additional cellular scope record, and it passes. The full resolver still stops at the documented `mt6989-local` feed because the reusable MT6989 `soc.toml` repository has not been split out; this is an explicit configuration follow-up, not a hidden build claim.

## Build and package

Copy `local.toml.example` to the ignored `local.toml` and provide paths for the main UniXNU checkout, the private UEFI checkout and the donor IPSW. Then run:

```sh
scripts/package.sh --check --os 17 --uefi ../uefi-rothko
scripts/package.sh --os 17 --uefi ../uefi-rothko
```

The wrapper delegates to the main checkout's official `unixnu release` command. Do not commit the private UEFI tree, IPSW, proprietary firmware, generated payload or boot images.

## Evidence state

The device record is deliberately conservative:

- **Observed on the device:** early XNU K1-K4/MMU/serial, eight-core startup/timer/IPI activity, Goodix reports and HID queue activity, XNU UFS HS-G5 dual-lane/LU2 reads and root matching, and the activation page on the unx7 baseline.
- **Implemented but not closed on the device:** UI touch interaction, physical-key wake, long-term display/userland stability and the fixed FDVFS request's effect on perceived lag.
- **Pending:** native GPU, Wi-Fi, Bluetooth, cellular, audio and battery service.
- **Unresolved:** the later UEFI/BROM incident. The record does not claim a proven LUN0 write, preloader overwrite or safe recovery state.

See [`porting.toml`](porting.toml) for the machine-readable record and [`docs/verification.md`](docs/verification.md) for the compact evidence summary.

## References

- [Port a device](https://unixnu.com/porting/)
- [Device repository requirements](https://unixnu.com/porting/device-repository)
- [Porting record format](https://unixnu.com/porting/records)
- [UFS adaptation](https://unixnu.com/porting/ufs)
- [Bring-up and log capture](https://unixnu.com/porting/bring-up)

