#!/bin/sh
# launch.sh's side of Play, run for real with a fake app, a fake emulator,
# fake hooks and a fake governor, all of which log what they were given.
#
#   tests/test_launch_sh.sh pak/launch.sh

set -u
LAUNCH_SH=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
FAILURES=0

fail() {
    echo "FAIL $CASE: $*" >&2
    FAILURES=$((FAILURES + 1))
}

expect_log() {
    expected=$1
    actual=$(cat "$T/log" 2>/dev/null)
    [ "$actual" = "$expected" ] || fail "log was:
$actual
expected:
$expected"
}

# A fresh pak and SD card. On its n-th run the fake app hands off whatever
# is in $T/handoff.<n>, so each case says what the "player" chose each time
# the app opened; with no such file, the player quit.
setup() {
    CASE=$1
    T=$(mktemp -d "${TMPDIR:-/tmp}/prgc-launch-XXXXXX")
    mkdir -p "$T/pak" "$T/emu" "$T/logs" "$T/system/bin" "$T/userdata/.hooks/pre-launch.d" \
        "$T/userdata/.hooks/post-launch.d"
    cp "$LAUNCH_SH" "$T/pak/launch.sh"
    : >"$T/game.sfc"

    cat >"$T/pak/prgc-tracker" <<EOF
#!/bin/sh
n=\$(( \$(cat "$T/runs" 2>/dev/null || echo 0) + 1 )); echo \$n >"$T/runs"
echo "app \$n [\$*]" >>"$T/log"
[ -f "$T/handoff.\$n" ] && cp "$T/handoff.\$n" /tmp/prgc-tracker-launch
exit 0
EOF
    cat >"$T/emu/launch.sh" <<EOF
#!/bin/sh
echo "emu [\$1] slot=\$(cat /tmp/resume_slot.txt)" >>"$T/log"
EOF
    cat >"$T/system/bin/governor.sh" <<EOF
#!/bin/sh
echo "\$1" >>"$T/governor"
EOF
    for phase in pre post; do
        cat >"$T/userdata/.hooks/$phase-launch.d/log.sync.sh" <<EOF
#!/bin/sh
echo "$phase-hook type=\$HOOK_TYPE rom=\$HOOK_ROM_PATH emu=\$HOOK_EMU_PATH" >>"$T/log"
EOF
    done
    chmod +x "$T/pak/prgc-tracker" "$T/emu/launch.sh" "$T/system/bin/governor.sh" \
        "$T/userdata/.hooks"/*/log.sync.sh
    # Stands in for NextUI's run_hooks.sh: runs every script in a hook
    # directory with HOOK_PHASE set.
    cat >"$T/system/bin/run_hooks.sh" <<'HOOKS'
#!/bin/sh
case "$1" in pre-*) export HOOK_PHASE=pre ;; post-*) export HOOK_PHASE=post ;; esac
for script in "$USERDATA_PATH/.hooks/$1"/*.sh; do
    [ -f "$script" ] && "$script"
done
exit 0
HOOKS
    chmod +x "$T/system/bin/run_hooks.sh"
    rm -f /tmp/prgc-tracker-launch /tmp/resume_slot.txt
}

# What the player does on run $1 of the app: play $2 from slot $3.
play_on_run() {
    printf '%s\n%s\n%s\n' "$T/emu/launch.sh" "$2" "${3-}" >"$T/handoff.$1"
}

run() {
    LOGS_PATH="$T/logs" SYSTEM_PATH="$T/system" USERDATA_PATH="$T/userdata" \
        SDCARD_PATH="$T" PLATFORM=h700 sh "$T/pak/launch.sh"
    STATUS=$?
}

cleanup() {
    rm -rf "$T"
    rm -f /tmp/prgc-tracker-launch /tmp/resume_slot.txt
}

# ── Cases ─────────────────────────────────────────────────────────────────────

setup "quitting without playing ends at once"
run
[ "$STATUS" -eq 0 ] || fail "exit status $STATUS"
expect_log "app 1 []"
cleanup

setup "a played game returns to the app, which quits next time"
play_on_run 1 "$T/game.sfc" 3
run
expect_log "app 1 []
pre-hook type=rom rom=$T/game.sfc emu=$T/emu/launch.sh
emu [$T/game.sfc] slot=3
post-hook type=rom rom=$T/game.sfc emu=$T/emu/launch.sh
app 2 [--returning]"
[ ! -f /tmp/prgc-tracker-launch ] || fail "hand-off file left behind"
cleanup

setup "the loop goes on as long as the player keeps playing"
play_on_run 1 "$T/game.sfc" 1
play_on_run 2 "$T/game.sfc" 8
run
[ "$(grep -c '^emu ' "$T/log")" -eq 2 ] || fail "expected two games"
[ "$(tail -1 "$T/log")" = "app 3 [--returning]" ] || fail "did not return after the second game"
cleanup

setup "a missing or bad slot starts fresh"
play_on_run 1 "$T/game.sfc" ""
play_on_run 2 "$T/game.sfc" "nine"
run
[ "$(grep '^emu ' "$T/log" | sed 's/.*slot=//' | tr '\n' ' ')" = "8 8 " ] || fail "slots: $(grep '^emu ' "$T/log")"
cleanup

setup "a missing ROM launches nothing and ends the loop"
play_on_run 1 "$T/nope.sfc" 2
run
expect_log "app 1 []"
grep -q "missing emulator or ROM" "$T/logs/prgc-tracker.txt" || fail "not logged"
cleanup

setup "paths with spaces and quotes survive"
: >"$T/Super Ghouls 'N Ghosts (USA).zip"
play_on_run 1 "$T/Super Ghouls 'N Ghosts (USA).zip" 0
run
grep -q "^emu \[$T/Super Ghouls 'N Ghosts (USA).zip\] slot=0$" "$T/log" || fail "$(cat "$T/log")"
cleanup

setup "the CPU is at full speed for the game and saves power in the app"
play_on_run 1 "$T/game.sfc" 1
run
[ "$(tr '\n' ' ' <"$T/governor")" = "powersave performance powersave " ] || fail "governor: $(tr '\n' ' ' <"$T/governor")"
cleanup

setup "no hook runner (older NextUI) still plays"
play_on_run 1 "$T/game.sfc" 1
rm "$T/system/bin/run_hooks.sh"
run
grep -q "^emu " "$T/log" || fail "game did not run"
grep -q "hook" "$T/log" && fail "hooks ran without a runner"
cleanup

if [ "$FAILURES" -gt 0 ]; then
    echo "$FAILURES case(s) failed" >&2
    exit 1
fi
exit 0
