# Offline audit — 2026-11-07

This audit reads local source, saved logs, patch inputs and prior build records.
It does not qualify a new image or a recovered handset. unx16 remains the
positive UI/root baseline, unx17 is withdrawn after BROM feedback, and unx18 is
an offline firmware candidate.

## Repository and release findings

- Official Format 2 status resolves XNU to OS-profile Apple `d8b802`, although
  the raw device feed says `woaphone/xnu-mtk` branch `rothko`, `fd4edc7`.
- The selected graph has 77 patches. Its full application cannot be claimed
  verified from a record checker or path-resolution result. An isolated Git
  index replay of the 18 device patches from `53bc420` does reproduce exactly
  tree `31ccd3607150c341acdd4da7ee6341716a95969f` of `fd4edc7`.
- Current official driver builder choices do not include `NativeMtkUfs` or
  `MtkCpuFreq`. Board InputProbe and the FDVFS call need their actual
  archive/startup integration qualified, not just presence as source files.
- The main firmware adapter invokes BaseTools with CLANGPDB; the qualified
  Rothko route is `DeviceBuild.py` plus stuart/CLANGDWARF. These are not
  interchangeable build records.
- The older local sibling main checkout does not have the release command.
  The wrapper now accepts `UNIXNU_BASE` to select a compatible main checkout.
- Source overrides belong in device `local.toml`. Build inputs use `[build]`
  through `--config`, with `build.local.toml.example` supplied separately.
- The prelink path now names selected `XnuPrelink`, and DEVICE_MODEL is the
  numeric value used by the qualified firmware build command.
- UEFI README no longer names a nonexistent MT6989Pkg directory; the source
  retains inherited MediaTek package namespaces alongside the Rothko platform.
- Package CI now offers only the declared iPadOS 17 profile, and checkout of
  the public UEFI repository can use the normal GitHub token when no private
  token is configured. No CI or remote operation was run during this audit.
- Board DTS is a profile/inventory input. The ADT generator accepts arguments
  and uses constants; it does not consume the DTS. The DTS memory length is
  7 GiB, payload DRAM intent is 24 GiB and owned-memory intent is 8 GiB. The
  retained static Android memory node describes only 1 GiB; live LK mblocks
  and UEFI exclusions are required to reconcile these representations.

## Frequency and UFS findings

| Contract | Source result | Remaining limit |
| --- | --- | --- |
| Persistent `bRefClkFreq` | unx17 selects 26 MHz and conditionally writes the device attribute; unx18 suppresses that path | No same-attempt original value, write result or readback; Linux 52 MHz support is not proof of the device's stored value |
| Host/link state | unx18 skips PHY power-on and device-reset/refclk SMC, but still toggles RTFF SMC BIT9 and modifies HCI/AHIT/options/link state | It is not full LK-state adoption, and BROM causation is unproved |
| UFS CDB policy | BlockIo is read-only; pass-through denies eight modifying opcodes | Other CDBs are forwarded; WRITE SAME is one unblocked modifying opcode. Complete media/device-state protection is open |
| Architectural timer | Stock effective DT and minimal ADT use 13,000,000 Hz | No evidence of a 26/52 MHz timer mismatch |
| CPU FDVFS | Firmware LUT selection, 26 MHz vote units, domain masks 0x0f/0x70/0x80 and fixed <=2 GHz boot policy are present | Request readback is not measured frequency; no runtime governor, thermal policy or demonstrated lag cure |

UFS NOP/descriptor DMA completion remains unverified after the software cache
fix. Generic LUN0-7 enumeration still exists. The missing-object Mu_Basecore
pin is unavailable in this offline checkout, so the retained WSL source and
the public submodule object must not be described as independently compared.

## Previously unresolved runtime bugs

- unx16 confirms root APFS and sustained UFS reads. It does not resolve current
  UEFI NOP/DMA completion or the later BROM incident.
- The 8 GiB bootstrap pool defect was reproduced and corrected. Memory size
  alone does not establish the cause of lock/unlock black screen or lag.
- Console locking and panic formatting were corrected. The original Ticket
  spinlock capture lacks owner/address/caller; no unique root cause is proven.
- Goodix report/HID transport is observed. UI touch consumption, three-key
  interaction/wake and stopped CMD presentation remain open.
- The key thread waits for the digitizer-open flag before publishing or
  polling physical keys. CPU performance startup is called from the
  touch-enabled board probe. These dependencies are source-confirmed;
  their contribution to wake failure or lag is not established by the logs.
- Missing Managed Preferences target directories were fixed. PerfPower's
  exact common assertion branch and elimination of all aborts are unverified.
- lsd's primary-persona container failure, ScreenTime's exception name/reason
  and the MAC policy returning EPERM are not identified by the retained logs.
- NFG1000A register backend is tested but lacks I2C11 and IOPMPowerSource.
  MT6639 Wi-Fi and UART3 Bluetooth have no implemented OS services. Native
  GPU, audio, cellular and complete sleep/resume remain unfinished.

The parent K70U workspace retains the detailed per-run logs, source exports
and full audit evidence. Candidate-specific identities are indexed in
`verification.md`; the older repeated `d8def1f` entry is not the source
identity for every device result.
