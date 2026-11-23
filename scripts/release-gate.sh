#!/usr/bin/env bash
# Gate comune per i workflow di pacchetto: nessun artefatto puo' essere
# preparato se il build corrente non supera i test e non produce l'eseguibile.
# La verifica dell'artefatto finale avviene dopo il packaging nei workflow;
# non richiede certificati Apple, notarizzazione o Developer ID.
set -euo pipefail

BUILD_DIR="${1:?build directory required}"
EXECUTABLE="${2:?executable path required}"
MANIFEST_FILE="${3:-$BUILD_DIR/decodxlog-capabilities.json}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT_DIR/scripts/check-doc-version.sh" "$ROOT_DIR" "$MANIFEST_FILE"

cmake --build "$BUILD_DIR"
if [[ -n "${DECODXLOG_CTEST_EXCLUDE_REGEX:-}" ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure \
        -E "$DECODXLOG_CTEST_EXCLUDE_REGEX"
else
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
test -x "$EXECUTABLE"
startup_dir="$(mktemp -d "${TMPDIR:-/tmp}/decodxlog-release-gate.XXXXXX")"
startup_probe="$startup_dir/startup.png"
trap 'rm -rf "$startup_dir"' EXIT
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" DECODXLOG_DATA_ROOT="$startup_dir/data" \
    "$EXECUTABLE" --settings "$startup_dir/settings" --db "$startup_dir/log.sqlite" --port 0 \
    --show badge --grab "$startup_probe"
test -s "$startup_probe"
echo "startup smoke test passed with isolated settings and log"
echo "release gate passed: $EXECUTABLE"
