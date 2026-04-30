# mstpctl ubus Integration for OpenWrt

This document explains the OpenWrt enhancement that adds `ubus` control to `mstpctl`, including available `ubus` methods and practical shell examples.

## Overview

The OpenWrt integration keeps `mstpd` as the STP engine and adds a `ubus` frontend in `mstpctl`.

Control path:

`ubus client -> mstpctl (ubus daemon) -> mstpd control socket`

This avoids two independent configuration planes and keeps one control frontend.

## What Was Added

1. `mstpctl` daemon mode:
- New CLI option: `-D` / `--daemon`
- Starts `mstpctl` as a background `ubus` service process
- Cannot be combined with normal CLI commands or batch input

2. Optional ubus build support:
- Configure flag: `--enable-ubus`
- Requires: `libubus` and `libubox`
- Compile-time guard: `HAVE_UBUS`

3. New ubus object and methods:
- Object name: `ustp`
- Methods:
  - `add_bridge`
  - `bridge_state`
  - `bridge_status`

4. OpenWrt service integration:
- Init script starts both:
  - `mstpctl -D` (ubus service)
  - `mstpd -d` (daemon)

## OpenWrt Package Notes

The OpenWrt package in
`/home/albrecht/src/openwrt/package/network/services/mstpd-ustp`
uses:

- `CONFIGURE_ARGS += --enable-ubus`
- runtime deps on `libubox` and `libubus`
- init script `/etc/init.d/mstpd` that launches `mstpctl -D` before `mstpd -d`

## ubus API

Use the object `ustp`:

```sh
ubus list | grep ustp
```

Expected output includes:

```text
ustp
```

### 1) add_bridge

Stores bridge config in `mstpctl` for later activation.

Method:

```text
ubus call ustp add_bridge '{...}'
```

Input fields:
- required:
  - `name` (string): bridge name, for example `br-lan`
- optional:
  - `proto` (string): `stp`, `rstp`, or `mstp` (default behavior is RSTP)
  - `forward_delay` (int)
  - `hello_time` (int)
  - `max_age` (int)
  - `ageing_time` (int)

Success reply:

```json
{"ok": 1}
```

Example:

```sh
ubus call ustp add_bridge '{
  "name":"br-lan",
  "proto":"rstp",
  "forward_delay":15,
  "hello_time":2,
  "max_age":20,
  "ageing_time":300
}'
```

### 2) bridge_state

Enables or disables STP handling for a bridge.

Method:

```text
ubus call ustp bridge_state '{...}'
```

Input fields:
- `name` (string): bridge name
- `enabled` (bool): `true` to add/apply, `false` to remove

Important behavior:
- Enabling requires prior `add_bridge` for the same bridge.
- `add_bridge` data is cached in `mstpctl` and expires after about 60 seconds if unused.

Success reply:

```json
{"ok": 1}
```

Enable example:

```sh
ubus call ustp bridge_state '{"name":"br-lan", "enabled":true}'
```

Disable example:

```sh
ubus call ustp bridge_state '{"name":"br-lan", "enabled":false}'
```

### 3) bridge_status

Returns CIST bridge status, mapped from `mstpd` status.

Method:

```text
ubus call ustp bridge_status '{"name":"br-lan"}'
```

Input fields:
- `name` (string): bridge name

Reply fields include:
- `stp_enabled`
- `enabled`
- `bridge_id`
- `designated_root`
- `regional_root`
- `root_port`
- `path_cost`
- `internal_path_cost`
- `max_age`
- `bridge_max_age`
- `forward_delay`
- `bridge_forward_delay`
- `tx_hold_count`
- `max_hops`
- `hello_time`
- `ageing_time`
- `protocol_version`
- `time_since_topology_change`
- `topology_change_count`
- `topology_change`
- `topology_change_port`
- `last_topology_change_port`

Example:

```sh
ubus call ustp bridge_status '{"name":"br-lan"}'
```

## End-to-End Shell Examples

### Start and verify services

```sh
/etc/init.d/mstpd restart
pgrep -af 'mstpd|mstpctl'
ubus list | grep ustp
```

### Configure and enable STP on br-lan

```sh
ubus call ustp add_bridge '{"name":"br-lan","proto":"rstp","hello_time":2,"max_age":20,"forward_delay":15}'
ubus call ustp bridge_state '{"name":"br-lan","enabled":true}'
ubus call ustp bridge_status '{"name":"br-lan"}'
```

### Disable bridge STP handling

```sh
ubus call ustp bridge_state '{"name":"br-lan","enabled":false}'
```

## Troubleshooting

1. `This mstpctl build has no ubus support`
- Rebuild with `--enable-ubus`
- Ensure `libubus` and `libubox` are present at build time

2. `ubus` object not present (`ustp` missing)
- Check `mstpctl` daemon is running: `pgrep -af "mstpctl -D"`
- Restart service: `/etc/init.d/mstpd restart`

3. `bridge_state` enable fails
- Run `add_bridge` first for the bridge
- Ensure bridge interface exists (for example `br-lan`)
- Enable shortly after `add_bridge` (config cache expires)

4. Status call fails
- Verify `mstpd` is running: `pgrep -x mstpd`
- Verify bridge exists and is known to system networking

## Notes

- The `network.device` `stp_init` subscription is used internally to ingest netifd bridge config events.
- User-facing control should use `ubus call ustp ...` methods documented above.
