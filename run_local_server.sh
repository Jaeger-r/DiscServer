#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build/codex-verify}"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target DiskServerApp

if [[ "$(uname -s)" == "Darwin" ]]; then
    QT_ROOT="${JAEGER_QT_ROOT:-$HOME/Qt/6.11.1/macos}"
    SOURCE_PLUGIN="$QT_ROOT/plugins/sqldrivers/libqsqlpsql.dylib"
    LIBPQ="/opt/homebrew/opt/postgresql@17/lib/postgresql/libpq.5.dylib"
    TARGET_PLUGIN="$BUILD_DIR/sqldrivers/libqsqlpsql.dylib"

    if [[ ! -f "$SOURCE_PLUGIN" ]]; then
        echo "Qt PostgreSQL driver not found: $SOURCE_PLUGIN" >&2
        exit 1
    fi
    if [[ ! -f "$LIBPQ" ]]; then
        echo "PostgreSQL libpq not found: $LIBPQ" >&2
        exit 1
    fi

    mkdir -p "$BUILD_DIR/sqldrivers"
    cp "$SOURCE_PLUGIN" "$TARGET_PLUGIN"
    if otool -L "$TARGET_PLUGIN" | grep -q '/Applications/Postgres.app/Contents/Versions/14/lib/libpq.5.dylib'; then
        install_name_tool -change \
            /Applications/Postgres.app/Contents/Versions/14/lib/libpq.5.dylib \
            "$LIBPQ" \
            "$TARGET_PLUGIN"
    fi
    codesign --force --sign - "$TARGET_PLUGIN" >/dev/null
    export QT_PLUGIN_PATH="$BUILD_DIR${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
fi

exec "$BUILD_DIR/DiskServer"
