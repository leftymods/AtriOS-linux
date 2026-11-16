#!/bin/bash
# Build the selected board kernel; use compile.sh image for a complete image.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec bash "$PROJECT_ROOT/compile.sh" TARGET=kernel "$@"
