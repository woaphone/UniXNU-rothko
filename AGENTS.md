# AGENTS.md — UniXNU-rothko

Read the official UniXNU guides before changing this repository:

- `Port a device`
- `Device repository`
- `Porting records`
- `Driver adaptation checklist`
- `UFS`, `bring-up` and `log capture`

The machine-readable source of truth is `board.toml` plus `porting.toml`. Keep the
record aligned with actual source, host and device evidence. Use `implemented`
for source work, `host-verified` for reproducible host results and
`device-verified` only when a captured device result identifies the image and
revision. Keep unresolved limits and the next measurement in the same area.

Keep UEFI changes in the separate private `challengethepeek/uefi-rothko`
repository. Device-specific XNU changes belong in `patches/xnu` and must be
listed in `patches/xnu/series` in application order. Do not copy IPSW, firmware,
userdata images, kernelcache binaries, pstore logs or generated images into this
repository; record their hashes and locations in the verification record or the
parent bring-up reports.
