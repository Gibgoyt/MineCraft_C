#!/bin/sh
# Plan 008 phase 01: what the card and the machine were doing, once a second, beside a bench run, until killed.
# Usage: bench/world/gpu_watch.sh <file.csv>
# Writes nvidia-smi's clocks, power, power limit, temperature, use and throttle reasons to <file.csv>, and `sensors`
# to <file.csv>.sensors, each line with the time. Read-only: it asks, and changes nothing. Run it inside the same
# tools/test-or-build.sh call as the bench, killed when the bench ends:
#   tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu.csv & w=$!; timeout 900 \
#       build/profile/bin/bench_world_shake ...; s=$?; kill $w; wait $w; exit $s'
set -u

if [ "$#" -ne 1 ]; then
    echo "usage: bench/world/gpu_watch.sh <file.csv>" >&2
    exit 64
fi
out=$1
mkdir -p "$(dirname "$out")"

nvidia-smi --query-gpu=timestamp,clocks.gr,clocks.mem,power.draw,enforced.power.limit,temperature.gpu,utilization.gpu,clocks_throttle_reasons.active \
    --format=csv -l 1 >"$out" 2>&1 &
smi=$!

stop() {
    kill "$smi" 2>/dev/null
    wait "$smi" 2>/dev/null
    exit 0
}
trap stop TERM INT HUP

while :; do
    {
        date '+%Y-%m-%dT%H:%M:%S'
        sensors 2>/dev/null | grep -E 'Package|Core|edge|temp|fan|CPU|GPU' || true
    } >>"$out.sensors"
    sleep 1 &
    wait $!
done
