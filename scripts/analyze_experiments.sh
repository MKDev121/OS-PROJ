#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Experiment Data Analysis Runner Wrapper
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_SCRIPT="$SCRIPT_DIR/analyze_experiments.py"

chmod +x "$PYTHON_SCRIPT" 2>/dev/null || true

exec python3 "$PYTHON_SCRIPT" "$@"
