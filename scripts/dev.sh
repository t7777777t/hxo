#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Build native first
"$SCRIPT_DIR/build-native.sh"

# Run example
cd "$PROJECT_ROOT"
bun run examples/hello-ffi/main.ts
