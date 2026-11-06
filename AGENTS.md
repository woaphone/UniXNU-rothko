# AGENTS.md — UniXNU-rothko

This is the device repository for rothko. Keep UEFI changes in the separate
uefi-rothko repository. Device-specific XNU changes belong in patches/xnu and
must be listed in patches/xnu/series in application order.

Do not commit proprietary IPSW, userdata images, kernelcache binaries or build
outputs. Record their SHA-256 in board.toml, reports or porting.toml instead.
