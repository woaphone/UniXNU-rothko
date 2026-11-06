# UniXNU-rothko

Device repository for the Xiaomi Redmi K70 Ultra (`rothko`, MediaTek MT6989 / Dimensity 9300).

This repository contains the non-UEFI board port: XNU patch series, kernel profile, MT6989/UFS/Data, display, input, CPU-frequency and board kit sources. The UEFI firmware remains a separate `uefi-rothko` repository and is referenced as an external input.

The port targets the iPad7,1 / iPadOS 17.7.11 / build 21H461 userspace donor used by the established UniXNU sheng/yunluo ports. The board has eight CPUs, a 13 MHz timer, MT6989 GICv3, UFS userdata and a 1220x2712 panel.

## Layout

- `board.toml` — board, IPSW, userdata, kernel, payload and external UEFI contract.
- `configs/rothko_defconfig` — XNU feature profile.
- `patches/xnu/series` — ordered XNU patches from the rothko base branch to the current port.
- `drivers/` — board and device drivers used by the tested kernel.
- `kits/rothko/` — Goodix/input and legacy framebuffer ABI bindings.
- `scripts/make-rothko-adt.py` — ADT generator entry point.
- `dts/rothko.dts` — reviewable board resource record.
- `porting.toml` — area-by-area implementation and verification record.

## Build contract

From the main UniXNU checkout, resolve this repository with the actual defconfig, fetch the pinned sources, inspect the tree, then build in the documented order:

```sh
./unixnu defconfig rothko_defconfig -D ../UniXNU-rothko
./unixnu fetch -D ../UniXNU-rothko
./unixnu status --json -D ../UniXNU-rothko
./unixnu tree -D ../UniXNU-rothko
python3 scripts/porting-record.py check --device ../UniXNU-rothko
```

The current offline build used the same official kernel, prelink, payload and boot image chain recorded in the project reports. Hardware evidence is recorded in the parent K70U bring-up workspace; no UEFI source is copied here.

## Verification state

The tested non-UEFI path has evidence for early XNU entry, eight-core startup, CMD display presentation, Goodix input, UFS LU2 reads, root matching and activation-page reachability. CPU FDVFS requests are integrated as a fixed boot policy. Wi-Fi, Bluetooth, battery service and dynamic CPU governor remain pending. The earlier BROM incident is recorded as unresolved and is not hidden by this repository.

## License

BSD-2-Clause-Patent. See `package/LICENSE`.
