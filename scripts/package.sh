#!/usr/bin/env bash
# Official device-repository wrapper. Board-specific values belong in board.toml.
set -euo pipefail
DEVICE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
UNIXNU=$(python3 - "$DEVICE/board.toml" <<'PY'
import sys, tomllib
package = tomllib.load(open(sys.argv[1], "rb")).get("package", {})
if "unixnu" not in package:
    raise SystemExit("board.toml needs [package] unixnu")
print(package["unixnu"])
PY
)
case "$UNIXNU" in
  /*) ROOT=$UNIXNU ;;
  *) ROOT=$(cd "$DEVICE/$UNIXNU" && pwd) ;;
esac
exec python3 "$ROOT/unixnu" release -D "$DEVICE" "$@"
