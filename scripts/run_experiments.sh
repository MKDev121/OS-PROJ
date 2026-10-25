#!/usr/bin/env bash
# ==============================================================================
# Interactive Session Protector — Experiment Automation Runner Wrapper
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_SCRIPT="$SCRIPT_DIR/run_experiments.py"

chmod +x "$PYTHON_SCRIPT" 2>/dev/null || true

# Execute python experiment runner with all arguments passed through
exec python3 "$PYTHON_SCRIPT" "$@"
