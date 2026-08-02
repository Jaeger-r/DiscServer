#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build/codex-verify}"

cd "$BUILD_DIR"
export QT_PLUGIN_PATH="$BUILD_DIR${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
exec "$BUILD_DIR/DiskServer"
