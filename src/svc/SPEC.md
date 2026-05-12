# svc — Service Helper Command

## Purpose

Provide a lightweight shell command for PM2-like operations on systemd
services focused on YAHA system units.

## Public interface

### `svc` executable

Supported subcommands:

- `list`
- `log`

`svc list [pattern]` prints a table with one row per matching system service
and the following
columns:

- `SERVICE`: Unit name (`*.service`)
- `STATE`: Combined active/sub state from systemd
- `RESTARTS`: Value of `NRestarts`
- `MEMORY`: Value of `MemoryCurrent` converted to human-readable units
- `PID`: Main process id (`MainPID`)
- `LAST_START`: Value of `ExecMainStartTimestamp`

`svc log <service> [lines]` follows the service logs using `journalctl`:

- `<service>` accepts both short names (`broker`, `brkconn`, `httpmqtt`) and unit names (`broker.service`).
- `[lines]` is optional and defaults to `100`.
- output is live (`-f`) and continues until interrupted (`Ctrl+C`).

## Behavior

- Discovers service units from `systemctl list-units --type=service --all`.
- Reads per-service metadata with `systemctl show`.
- Applies a regex filter to unit names.
- Default filter matches the deployment-managed units `broker`, `filestore`, `msgstore`, `autom`, `valuesvc`, `brkconn`, `httpmqtt`, and `remotesvc`.
- Uses `MemoryCurrent` when available; otherwise falls back to process RSS
  from `/proc/<pid>/status` (`VmRSS`).
- Sorts services by unit name.
- Prints `-` for unavailable values.
- For `log`, normalizes missing `.service` suffix automatically.
- For `log`, streams logs with `journalctl -u <unit> -n <lines> -f --no-pager`.
- Returns non-zero when `systemctl` is missing or no supported subcommand is
  provided.

## Constraints

- Targets Linux hosts with systemd.
- Uses only POSIX shell utilities plus `awk`, `numfmt` (optional), and
  `systemctl`.
