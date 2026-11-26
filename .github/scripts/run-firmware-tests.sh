#!/bin/bash
# Discover every top-level firmware regression; do not collect legacy build or
# hardware suites recursively. The deferred Telegram bridge is a host tool.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"
mapfile -d '' firmware_tests < <(find tests -maxdepth 1 -type f -name 'test_*.py' \
    ! -name 'test_telegram_codex_bridge.py' -print0 | sort -z)
[ "${#firmware_tests[@]}" -gt 0 ]
exec python3 -B -m pytest -p no:cacheprovider -q "${firmware_tests[@]}" \
    tests/tier2_boundaries/test_aml_c_regression.py \
    tests/tier1_features/test_r1_aml_burn_protocol.py \
    tests/tier2_boundaries/test_r1_aml_burn_boundaries.py \
    tests/tier4_scenarios/test_e2e_aml_burn_simulation.py
