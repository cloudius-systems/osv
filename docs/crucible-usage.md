# Crucible Block Device Driver

The Crucible block device driver provides network-based block storage for OSv through integration with Oxide's Crucible distributed storage system.

## Overview

Crucible is a distributed, replicated block storage system that provides:
- Triple replication across three "downstairs" storage servers
- Strong consistency guarantees
- Network-based block device access
- Fault tolerance

This driver implements an "upstairs" client that connects to three Crucible downstairs servers and presents them as a standard block device in OSv.

## Building with Crucible Support

### Using the Profile

Build OSv with the Crucible driver profile:

```bash
./scripts/build conf_drivers_profile=crucible image=native-example
```

### Manual Configuration

Alternatively, enable the driver explicitly:

```bash
./scripts/build conf_drivers_crucible=1 image=native-example
```

## Boot Options

### Required Options

- `--crucible=host1:port1,host2:port2,host3:port3`

  Comma-separated list of three downstairs server addresses. Each address is in the format `hostname:port` or `ip:port`.

  Example: `--crucible=192.168.1.10:8000,192.168.1.11:8000,192.168.1.12:8000`

- `--crucible-uuid=UUID`

  UUID of the Crucible region to connect to. Must be in standard UUID format: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`

  Example: `--crucible-uuid=550e8400-e29b-41d4-a716-446655440000`

### Optional Options

- `--crucible-block-size=SIZE`

  Block size in bytes. Default: 512

  Example: `--crucible-block-size=4096`

- `--crucible-read-only`

  Mount the Crucible volume in read-only mode.

  Example: `--crucible-read-only`

## Usage Examples

### Basic Usage

Boot OSv with Crucible block device:

```bash
./scripts/run.py \
  --execute /usr/bin/myapp \
  --crucible=192.168.1.10:8000,192.168.1.11:8000,192.168.1.12:8000 \
  --crucible-uuid=550e8400-e29b-41d4-a716-446655440000
```

### With Custom Block Size

```bash
./scripts/run.py \
  --execute /usr/bin/myapp \
  --crucible=192.168.1.10:8000,192.168.1.11:8000,192.168.1.12:8000 \
  --crucible-uuid=550e8400-e29b-41d4-a716-446655440000 \
  --crucible-block-size=4096
```

### Read-Only Mount

```bash
./scripts/run.py \
  --execute /usr/bin/myapp \
  --crucible=192.168.1.10:8000,192.168.1.11:8000,192.168.1.12:8000 \
  --crucible-uuid=550e8400-e29b-41d4-a716-446655440000 \
  --crucible-read-only
```

## Multi-Volume Support

OSv supports up to 8 Crucible volumes simultaneously, enabling advanced storage configurations like ZFS RAID-Z.

### Boot Options for Multiple Volumes

Use indexed options (0-7) to configure multiple Crucible volumes:

```bash
./scripts/run.py \
  --crucible0=10.0.0.10:3000,10.0.0.10:3001,10.0.0.10:3002 \
  --crucible0-uuid=volume-0-uuid \
  --crucible1=10.0.0.10:3010,10.0.0.10:3011,10.0.0.10:3012 \
  --crucible1-uuid=volume-1-uuid \
  --crucible2=10.0.0.10:3020,10.0.0.10:3021,10.0.0.10:3022 \
  --crucible2-uuid=volume-2-uuid \
  -e /tests/native-example.so
```

This creates `/dev/crucible0`, `/dev/crucible1`, and `/dev/crucible2`.

### ZFS RAID-Z Example

Combine multiple Crucible volumes into a ZFS RAID-Z pool for enhanced fault tolerance:

```bash
# Inside OSv after boot with 3 volumes
zpool create -f \
  -o ashift=12 \
  -O compression=lz4 \
  -O atime=off \
  datapool raidz /dev/crucible0 /dev/crucible1 /dev/crucible2
```

This configuration provides:
- **Crucible-level replication**: Each volume replicated 3x across downstairs servers
- **ZFS-level redundancy**: RAID-Z parity protects against single volume failure
- **Combined fault tolerance**: Survives multiple simultaneous failures

For detailed setup instructions and examples, see:
- [ZFS RAID-Z on Crucible Guide](zfs-raidz-crucible-example.md)
- [Automated Test Script](../tests/zfs-crucible-raidz-l2arc.sh)

## Device Naming

The Crucible driver creates block devices with names in the format:

- `/dev/crucible0` - First Crucible device
- `/dev/crucible1` - Second Crucible device (if configured)
- `/dev/crucible2` - Third Crucible device (if configured)
- `/dev/crucible0.1` - First partition on first device
- `/dev/crucible0.2` - Second partition on first device

Up to 8 Crucible devices (crucible0-7) can be configured using indexed boot options.

## Accessing the Device

Once booted, the Crucible block device can be accessed like any other block device:

### Using with ZFS

```bash
# Create ZFS pool on Crucible device
zpool create mypool /dev/crucible0

# Mount ZFS filesystem
zfs set mountpoint=/mnt/data mypool
```

### Using with Other Filesystems

The device can be formatted with any filesystem OSv supports:

```bash
# The device appears as a standard block device
ls -l /dev/crucible*
```

## Troubleshooting

### Connection Issues

If the driver fails to connect to downstairs servers:

1. Verify all three servers are running and accessible
2. Check network connectivity from the OSv VM
3. Verify the UUID matches the region on the servers
4. Check server logs for connection attempts

Common error messages:

- `crucible_init: failed to connect to downstairs servers` - Network or server not available
- `crucible_init: invalid UUID format` - UUID string is malformed
- `crucible_init: expected 3 targets, got N` - Incorrect number of server addresses

### Boot Option Errors

- `crucible_init: missing required options` - Must provide both `--crucible` and `--crucible-uuid`
- Missing driver: Ensure OSv was built with `conf_drivers_crucible=1`

### Performance Issues

If I/O performance is poor:

1. Check network latency to downstairs servers
2. Try increasing block size (`--crucible-block-size=4096`)
3. Verify downstairs servers are not overloaded
4. Check for network bandwidth constraints

## Architecture

The driver implements:

1. **Connection Management** (`crucible-connection.cc/hh`)
   - TCP socket connections to downstairs servers
   - Connection lifecycle management

2. **Request Processing** (`crucible-request.cc/hh`)
   - I/O request queuing and tracking
   - Request/response correlation

3. **Protocol Implementation** (`crucible-messages.hh`, `crucible-bincode.hh`)
   - Crucible wire protocol
   - Bincode serialization/deserialization

4. **Client Logic** (`crucible-client.cc/hh`)
   - Upstairs client state machine
   - Replication and consistency

5. **Block Device Interface** (`crucible-blk.cc/hh`)
   - OSv block device integration
   - BIO request handling

## Limitations

Current limitations:

- Single Crucible volume per boot (can be extended to multiple)
- No encryption support yet
- No live migration support
- Total blocks must be queried from servers (not specified via boot option)

## Development

To modify the driver:

1. Source files are in `drivers/crucible-*.cc` and `drivers/crucible-*.hh`
2. Build system integration is in `Makefile` (line 904-909)
3. Boot option parsing is in `loader.cc`
4. Driver profile is in `conf/profiles/x64/crucible.mk`

### Testing

Build and test the driver:

```bash
# Build with debug symbols
./scripts/build mode=debug conf_drivers_crucible=1 image=native-example

# Run with verbose output
./scripts/run.py --verbose \
  --crucible=localhost:8000,localhost:8001,localhost:8002 \
  --crucible-uuid=550e8400-e29b-41d4-a716-446655440000
```

## References

- Oxide Computer Company: https://oxide.computer/
- Crucible source: https://github.com/oxidecomputer/crucible
- OSv block device documentation: docs/block-devices.md
- [ZFS RAID-Z on Crucible Example](zfs-raidz-crucible-example.md)
- [Crucible Testing Guide](crucible-testing.md)
- [Crucible Snapshots](crucible-snapshots.md)
