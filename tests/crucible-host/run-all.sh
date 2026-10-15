#!/bin/sh
# Copyright (C) 2026 Greg Burd.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
binary=${1:?binary}; output=${2:?output-directory}
mkdir -p "$output"
for scenario in control divergent dirty stale lagwrite prefix payload shutdown sendfail badtype identity admission trickle disconnects frontier allocation overflow pressure generation admissiontrailing quorum-badhash quorum-short quorum-contexts quorum-empty quorum-wrongkind quorum-trailing quorum-identity quorum-encrypted emptyzero quorum-hugecount quorum-hugelen quorum-fatal hostname ipv6 portoverflow portjunk portsign portzero portempty overlap snapshotloss watermark; do
    timeout 25 python3 "$root/faults.py" "$binary" "$scenario" > "$output/$scenario.log" 2>&1
    echo "PASS $scenario"
done

for mode in normal near cohort backpressure cancel eintr progress; do
    timeout 20 python3 "$root/admission-deadline.py" "$binary" "$mode" > "$output/admission-deadline-$mode.log" 2>&1
    echo "PASS admission-deadline-$mode"
done
