#!/bin/bash
# Full build only. Source publication is a separate, explicit operation.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec > >(tee "$PROJECT_ROOT/build_bg.log") 2>&1
printf '[%s] Starting full mainline build\n' "$(date -Is)"
bash "$PROJECT_ROOT/compile.sh" TARGET=image "$@"
printf '[%s] Build completed successfully\n' "$(date -Is)"
