#!/usr/bin/env bash
# Official device-repository wrapper. Board-specific values belong in board.toml.
set -euo pipefail
DEVICE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
UNIXNU=${UNIXNU_BASE:-$(python3 - "$DEVICE/board.toml" <<'PY'
import sys, tomllib
package = tomllib.load(open(sys.argv[1], "rb")).get("package", {})
if "unixnu" not in package:
    raise SystemExit("board.toml needs [package] unixnu")
print(package["unixnu"])
PY
)}
case "$UNIXNU" in
  /*) ROOT=$UNIXNU ;;
  *) ROOT=$(cd "$DEVICE/$UNIXNU" && pwd) ;;
esac
if [[ -f "$DEVICE/build.local.toml" ]]; then
  exec python3 "$ROOT/unixnu" release -D "$DEVICE" --config "$DEVICE/build.local.toml" "$@"
fi
exec python3 "$ROOT/unixnu" release -D "$DEVICE" "$@"
