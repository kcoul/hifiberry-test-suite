#!/usr/bin/env bash
# Plays one stimulus on two Pis at once (e.g. Linux and QNX), fetches both recordings
# and reports whether they have equal integrity.
#
#   dual_pi_compare.sh <stimulus.wav> <hostA> <hostB>
#
# Environment (all optional):
#   CLI_A, CLI_B     CLI path on each Pi            (default: HiFiBerryTestSuiteCLI)
#   ARGS_A, ARGS_B   extra 'run' options per Pi, e.g. "--output sndrpihifiberry --buffer 256"
#   REMOTE_DIR       scratch directory on the Pis   (default: /tmp/hifiberry-rt)
#   LOCAL_CLI        host build of the CLI          (default: HiFiBerryTestSuiteCLI)
#   OUT_DIR          local results directory        (default: results/<timestamp>)
#
# Exit status is that of the local 'compare': 0 equal, 2 different, 1 error.
set -euo pipefail

if [ $# -ne 3 ]; then
    sed -n '2,14p' "$0"
    exit 1
fi

stimulus=$1
name=$(basename "$stimulus" .wav)
remote_dir=${REMOTE_DIR:-/tmp/hifiberry-rt}
out_dir=${OUT_DIR:-results/$(date +%Y%m%d-%H%M%S)}

take() {
    local label=$1 host=$2 cli=$3 args=$4
    mkdir -p "$out_dir/$label"
    ssh "$host" "mkdir -p $remote_dir"
    scp -q "$stimulus" "$host:$remote_dir/$name.wav"
    ssh "$host" "$cli run --file $remote_dir/$name.wav $args" > "$out_dir/$label/run.log" 2>&1 || true
    scp -q "$host:$remote_dir/${name}_RTL.wav" "$host:$remote_dir/${name}_RTL.json" "$out_dir/$label/"
}

take A "$2" "${CLI_A:-HiFiBerryTestSuiteCLI}" "${ARGS_A:-}" & pid_a=$!
take B "$3" "${CLI_B:-HiFiBerryTestSuiteCLI}" "${ARGS_B:-}" & pid_b=$!

failed=0
wait $pid_a || failed=1
wait $pid_b || failed=1

if [ $failed -ne 0 ]; then
    echo "A take failed; see $out_dir/A/run.log and $out_dir/B/run.log" >&2
    exit 1
fi

set +e
"${LOCAL_CLI:-HiFiBerryTestSuiteCLI}" compare --file "$stimulus" \
    --recording-a "$out_dir/A/${name}_RTL.wav" \
    --recording-b "$out_dir/B/${name}_RTL.wav" \
    --json "$out_dir/comparison.json" | tee "$out_dir/comparison.txt"
exit "${PIPESTATUS[0]}"
