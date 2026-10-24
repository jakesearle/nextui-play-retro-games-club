#!/bin/sh
set -eu

APP_BIN="prgc-tracker"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAK_NAME=$(basename "$PAK_DIR")
PAK_NAME=${PAK_NAME%.pak}

cd "$PAK_DIR"

# curl is statically linked against our own OpenSSL, so there is no system
# trust store on the device — without this every HTTPS request fails.
if [ -f "$PAK_DIR/cacert.pem" ]; then
    export SSL_CERT_FILE="$PAK_DIR/cacert.pem"
fi

SHARED_USERDATA_ROOT=${SHARED_USERDATA_PATH:-"${HOME:-/tmp}/.userdata/shared"}
LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$APP_BIN.txt"
: >"$LOG_FILE"

exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching $PAK_NAME ($APP_BIN) at $(date) ==="
echo "platform=${PLATFORM:-unknown} device=${DEVICE:-unknown}"

if [ -f "${SYSTEM_PATH:-}/bin/governor.sh" ]; then
    sh "$SYSTEM_PATH/bin/governor.sh" powersave || echo "governor.sh failed, continuing" >&2
fi

exec "./$APP_BIN" "$@"
