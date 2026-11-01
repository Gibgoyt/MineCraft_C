#!/bin/sh
# Runs one heavy command (build, test, lint) while holding the machine-wide build lock, so
# parallel agents never build at the same time. See .claude/rules/test-or-build-mutex.md.
#
# The lock path is absolute on purpose: every git worktree of this repository must share it. A
# path relative to the checkout would give each worktree its own lock and no exclusion at all.
#
# flock -o closes the lock fd in the command, so a process the command leaves behind cannot keep
# the lock held. The command's exit status is returned unchanged.
set -u

MC_BUILD_LOCK=/tmp/mc_c_build.lock

if [ "$#" -eq 0 ]; then
    echo "usage: tools/test-or-build.sh <command> [args...]" >&2
    exit 64
fi

if ! flock -n -o "$MC_BUILD_LOCK" true; then
    echo "test-or-build: waiting for $MC_BUILD_LOCK" >&2
fi
exec flock -o "$MC_BUILD_LOCK" "$@"
