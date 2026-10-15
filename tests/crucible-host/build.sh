#!/bin/sh
# Copyright (C) 2026 Greg Burd.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
out=${1:?output}; sanitize=${2:-}
c++ -std=c++17 -g -O1 -Wall -Wextra -pthread -Wl,--wrap=send -Wl,--wrap=connect -Wl,--wrap=shutdown $sanitize -I"$root/tests/crucible-host/shim" -I"$root/drivers" "$root/tests/crucible-host/client.cc" "$root/drivers/crucible-client.cc" "$root/drivers/crucible-connection.cc" "$root/drivers/crucible-request.cc" "$root/drivers/crucible-hash.cc" -o "$out"
