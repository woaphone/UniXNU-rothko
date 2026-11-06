# rothko verification record

## Host

- XNU base branch: `origin/rothko` from `woaphone/xnu-mtk`.
- Final XNU: `fd4edc747395525a914987bb88f063b5e549ecc7`.
- Donor kernelcache SHA-256: `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.
- Official old Clang 18 compatibility route used; no new toolchain download.

## Device evidence

- Early XNU K1-K4 and MMU/serial initialization: observed.
- Eight CPU startup, timer and IPI activity: observed.
- Goodix sensor reports and HID queue activity: observed in kernel logs; UI touch interaction and wake-key behavior remain unverified.
- UFS HS-G5 dual-lane reads and root matching: observed in the unx16 follow-up evidence; the later unx17/BROM incident remains a separate unresolved firmware-state event.
- Activation page: observed on the unx7 baseline; this does not prove stable desktop, touch or wake behavior.
- BROM incident after the earlier UEFI candidate: not attributed to a proven LUN0 write; no recovery operation is part of this repository.

## Separate UEFI

The current UEFI candidate and its UFS refclk safety audit are in the sibling
uefi-rothko workspace/repository. This repository only records the firmware
contract and does not vendor the UEFI tree.

