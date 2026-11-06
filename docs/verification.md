# rothko verification record

## Host

- XNU base branch: `origin/rothko` from `woaphone/xnu-mtk`.
- Final XNU: `fd4edc747395525a914987bb88f063b5e549ecc7`.
- Donor kernelcache SHA-256: `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.
- Official old Clang 18 compatibility route used; no new toolchain download.

## Device evidence

- Early XNU K1-K4 and MMU/serial initialization: observed.
- Eight CPU startup, timer and IPI activity: observed.
- Goodix touch reports and key event path: observed in kernel logs.
- UFS HS-G5 dual-lane reads and root matching: observed in unx16/unx17 evidence.
- Activation page: observed on the unx7 baseline.
- BROM incident after the earlier UEFI candidate: not attributed to a proven LUN0 write; no recovery operation is part of this repository.

## Separate UEFI

The current UEFI candidate and its UFS refclk safety audit are in the sibling
uefi-rothko workspace/repository. This repository only records the firmware
contract and does not vendor the UEFI tree.
