#!/usr/bin/env bash
#
# Individual test scenarios for Crucible driver
# Run specific test cases manually for debugging

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSV_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOCK_SERVER="$SCRIPT_DIR/mock-crucible-downstairs.py"

# Default configuration
REGION_UUID="00000000-0000-0000-0000-000000000000"
BLOCK_SIZE=4096
TOTAL_SIZE=$((100 * 1024 * 1024))
PORTS=(8810 8820 8830)

usage() {
    cat << EOF
Usage: $0 <scenario> [options]

Scenarios:
  all-servers    - Start all 3 servers and boot OSv
  two-servers    - Start only 2 servers (quorum test)
  no-servers     - Boot without any servers
  interactive    - Start servers and drop to shell

Options:
  --help         - Show this help
  --ports=P1,P2,P3 - Custom ports (default: 8810,8820,8830)
  --uuid=UUID    - Custom region UUID
  --size=BYTES   - Custom device size

Examples:
  $0 all-servers
  $0 two-servers --ports=9810,9820,9830
  $0 interactive
EOF
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --help)
                usage
                exit 0
                ;;
            --ports=*)
                IFS=',' read -ra PORTS <<< "${1#*=}"
                ;;
            --uuid=*)
                REGION_UUID="${1#*=}"
                ;;
            --size=*)
                TOTAL_SIZE="${1#*=}"
                ;;
            *)
                SCENARIO="$1"
                ;;
        esac
        shift
    done
}

build_crucible_args() {
    local targets=""
    for port in "${PORTS[@]}"; do
        if [ -n "$targets" ]; then
            targets="$targets,"
        fi
        # Use host IP as seen from QEMU guest (192.168.122.1 = TAP gateway = host)
        targets="${targets}192.168.122.1:${port}"
    done

    echo "--crucible=$targets --crucible-uuid=$REGION_UUID --crucible-block-size=$BLOCK_SIZE"
}

start_server() {
    local port=$1
    local log_file="/tmp/crucible-server-$port.log"

    echo "Starting server on port $port (log: $log_file)"
    python3 "$MOCK_SERVER" "$port" > "$log_file" 2>&1 &
    echo $!
}

wait_for_servers() {
    echo "Waiting for servers to be ready..."
    sleep 2

    for port in "${PORTS[@]}"; do
        # Check if port is available on host (we bind to 0.0.0.0)
        if ! nc -z 0.0.0.0 "$port" 2>/dev/null; then
            echo "Warning: Server on port $port not responding"
        else
            echo "Server on port $port is ready"
        fi
    done
}

scenario_all_servers() {
    echo "Scenario: All Servers"
    echo "===================="
    echo ""
    echo "Starting all 3 mock downstairs servers..."

    local pids=()
    for port in "${PORTS[@]}"; do
        pid=$(start_server "$port")
        pids+=("$pid")
    done

    wait_for_servers

    echo ""
    echo "Servers running. PIDs: ${pids[*]}"
    echo ""
    echo "Launching OSv..."

    local args
    args=$(build_crucible_args)

    cd "$OSV_ROOT"
    ./scripts/run.py $args --execute='/hello' --verbose

    echo ""
    echo "Cleaning up..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}

scenario_two_servers() {
    echo "Scenario: Two Servers (Quorum Test)"
    echo "===================================="
    echo ""
    echo "Starting only 2 out of 3 servers..."

    local pids=()
    for i in 0 1; do
        port="${PORTS[$i]}"
        pid=$(start_server "$port")
        pids+=("$pid")
    done

    echo "NOT starting server on port ${PORTS[2]}"

    wait_for_servers

    echo ""
    echo "Servers running: ${PORTS[0]}, ${PORTS[1]}"
    echo "Server missing: ${PORTS[2]}"
    echo ""
    echo "Launching OSv (should work with 2/3 quorum)..."

    local args
    args=$(build_crucible_args)

    cd "$OSV_ROOT"
    ./scripts/run.py $args --execute='/hello' --verbose

    echo ""
    echo "Cleaning up..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}

scenario_no_servers() {
    echo "Scenario: No Servers"
    echo "===================="
    echo ""
    echo "Launching OSv without any servers running..."
    echo "Expected: Boot completes with warnings, no device created"
    echo ""

    local args
    args=$(build_crucible_args)

    cd "$OSV_ROOT"
    ./scripts/run.py $args --execute='/hello' --verbose || true

    echo ""
    echo "Test completed"
}

scenario_interactive() {
    echo "Scenario: Interactive"
    echo "===================="
    echo ""
    echo "Starting all 3 mock downstairs servers..."

    local pids=()
    for port in "${PORTS[@]}"; do
        pid=$(start_server "$port")
        pids+=("$pid")
        echo "Server on port $port: PID $pid"
    done

    wait_for_servers

    echo ""
    echo "Servers are running. To test manually:"
    echo ""
    echo "  cd $OSV_ROOT"
    args=$(build_crucible_args)
    echo "  ./scripts/run.py $args --execute='/hello'"
    echo ""
    echo "Press Ctrl+C to stop servers and exit"
    echo ""

    trap "echo ''; echo 'Stopping servers...'; for pid in ${pids[*]}; do kill \$pid 2>/dev/null || true; done; exit 0" INT

    while true; do
        sleep 1
    done
}

main() {
    if [ $# -eq 0 ]; then
        usage
        exit 1
    fi

    SCENARIO=""
    parse_args "$@"

    if [ -z "$SCENARIO" ]; then
        echo "Error: No scenario specified"
        usage
        exit 1
    fi

    if [ ! -f "$MOCK_SERVER" ]; then
        echo "Error: Mock server not found: $MOCK_SERVER"
        exit 1
    fi

    case "$SCENARIO" in
        all-servers)
            scenario_all_servers
            ;;
        two-servers)
            scenario_two_servers
            ;;
        no-servers)
            scenario_no_servers
            ;;
        interactive)
            scenario_interactive
            ;;
        *)
            echo "Error: Unknown scenario: $SCENARIO"
            usage
            exit 1
            ;;
    esac
}

main "$@"
