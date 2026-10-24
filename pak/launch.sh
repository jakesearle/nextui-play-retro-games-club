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

set_governor() {
    if [ -f "${SYSTEM_PATH:-}/bin/governor.sh" ]; then
        sh "$SYSTEM_PATH/bin/governor.sh" "$1" || echo "governor.sh $1 failed, continuing" >&2
    fi
}

# Starts a game the way NextUI's menu would (openRom in nextui.c, and the
# launch loop in MinUI.pak/launch.sh); the app has already added it to
# Recently Played. Returns once the game is closed.
play() {
    EMU_LAUNCH=$1
    ROM=$2
    echo "launch: $EMU_LAUNCH $ROM"

    # Slot 8 is the hidden state minarch saves on quit, so the game picks up
    # where it was last left, exactly as it would from NextUI's menu.
    echo 8 >/tmp/resume_slot.txt

    if command -v gametimectl.elf >/dev/null 2>&1; then
        gametimectl.elf start "$ROM" || true
    fi

    # Hooks see the same variables NextUI's parse_hook_cmd exports for a ROM.
    HOOK_TYPE=rom
    HOOK_EMU_PATH=$EMU_LAUNCH
    HOOK_ROM_PATH=$ROM
    HOOK_CMD="'$EMU_LAUNCH' '$ROM'"
    HOOK_LAST=$(cat /tmp/last.txt 2>/dev/null || true)
    export HOOK_CMD HOOK_EMU_PATH HOOK_TYPE HOOK_ROM_PATH HOOK_LAST
    RUN_HOOKS="${SYSTEM_PATH:-}/bin/run_hooks.sh"

    set_governor performance
    if [ -x "$RUN_HOOKS" ]; then "$RUN_HOOKS" pre-launch.d; fi
    "$EMU_LAUNCH" "$ROM" || echo "launch: emulator exited with $?" >&2
    if [ -x "$RUN_HOOKS" ]; then "$RUN_HOOKS" post-launch.d; fi
    unset HOOK_CMD HOOK_EMU_PATH HOOK_TYPE HOOK_ROM_PATH HOOK_LAST
}

# Choosing Play leaves the emulator pak's launch.sh and the ROM here, one per
# line, and exits. NextUI's own hand-off, /tmp/next, is deleted as soon as
# this script returns, so the game is started from here instead, and when it
# closes the app opens again (with --returning) rather than dropping back to
# NextUI. Quitting the app without choosing Play ends the loop.
LAUNCH_FILE=/tmp/prgc-tracker-launch
rm -f "$LAUNCH_FILE"

while :; do
    set_governor powersave
    "./$APP_BIN" "$@" || echo "$APP_BIN exited with $?" >&2

    [ -f "$LAUNCH_FILE" ] || break
    EMU_LAUNCH=
    ROM=
    { IFS= read -r EMU_LAUNCH || true; IFS= read -r ROM || true; } <"$LAUNCH_FILE"
    rm -f "$LAUNCH_FILE"
    if [ ! -f "$EMU_LAUNCH" ] || [ ! -f "$ROM" ]; then
        echo "launch: missing emulator or ROM: '$EMU_LAUNCH' '$ROM'" >&2
        break
    fi

    play "$EMU_LAUNCH" "$ROM"
    set -- --returning
done
exit 0
