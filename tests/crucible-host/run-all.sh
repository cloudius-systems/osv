#!/bin/sh
# Copyright (C) 2026 Greg Burd.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
binary=${1:?binary}; output=${2:?output-directory}
mkdir -p "$output"
# Driver codec against frames from upstream's own encoder (see golden-v14.rs).
c++ -std=c++17 -Wall -Wextra -I"$root/shim" -I"$root/../../drivers" "$root/golden.cc" -o "$output/golden"
"$output/golden" "$root/golden-v14.txt"
for scenario in control divergent dirty stale lagwrite prefix payload shutdown sendfail badtype identity admission trickle disconnects frontier allocation overflow pressure generation admissiontrailing quorum-badhash quorum-short quorum-contexts quorum-empty quorum-wrongkind quorum-trailing quorum-identity quorum-encrypted emptyzero quorum-hugecount quorum-hugelen quorum-fatal hostname ipv6 portoverflow portjunk portsign portzero portempty overlap snapshotloss watermark v14 v13peers mixedversions v15only badbeenactive v14readonly; do
    timeout 25 python3 "$root/faults.py" "$binary" "$scenario" > "$output/$scenario.log" 2>&1
    echo "PASS $scenario"
done

for mode in normal near cohort backpressure cancel eintr progress; do
    timeout 20 python3 "$root/admission-deadline.py" "$binary" "$mode" > "$output/admission-deadline-$mode.log" 2>&1
    echo "PASS admission-deadline-$mode"
done
