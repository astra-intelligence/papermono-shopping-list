# Server

A small FastAPI app with a single SQLite file. It serves the [HTTP API](api.md),
the phone web UI at `/`, and the [PaperMono web flasher](../server/src/shopping_list/flasher)
at `/flash/` (flash a device straight from the phone/browser over WebSerial - see
[firmware.md](firmware.md#flash)). Python 3.11 or newer.

## Run it locally

```sh
cd server
python3 -m venv .venv
.venv/bin/pip install -e '.[dev]'
SHOPPING_LIST_CLASSIFIER=none .venv/bin/uvicorn shopping_list.main:app --reload --host 0.0.0.0
```

Then open `http://localhost:8000/`. The database is created at
`data/shopping.db` under the current directory.

Tests and lint:

```sh
.venv/bin/pytest
.venv/bin/ruff check . && .venv/bin/ruff format --check .
```

## Configuration

All configuration is through environment variables:

| Variable | Default | |
|----------|---------|--|
| `SHOPPING_LIST_DB` | `data/shopping.db` | SQLite file. Relative to the working directory. Created on first run. |
| `SHOPPING_LIST_CLASSIFIER` | `claude` | `claude` to auto-sort new items, `none` to disable. |
| `CLAUDE_BIN` | `claude` | Path to the Claude Code CLI, if it isn't on the service's `PATH`. |
| `SHOPPING_LIST_CLASSIFY_TIMEOUT` | `30` | Seconds before giving up on a classification. |
| `SHOPPING_LIST_FIRMWARE_DIR` | `data/firmware` | Directory holding OTA images, one `<version>.bin` each. Relative to the working directory. The systemd unit and Docker image point it at `firmware/` under their data directory. |
| `SHOPPING_LIST_FIRMWARE_VERSION` | *(unset)* | The version every device should be running. Unset means no OTA is offered. |

A new database starts with only the `Uncategorized` aisle. Add your real aisles
from the web UI (**Aisles**) in the order you walk the shop. The classifier only
runs once at least one aisle exists.

## Auto-sorting with Claude

When an item name has never been seen before, the server runs
`claude -p "<prompt>"` with the list of aisles, and files the item under the
aisle Claude names, or creates the new one it proposes. The answer is saved to
the catalog, so each distinct name costs at most one call, typically a few
seconds. If the CLI is missing, fails or times out, the item goes to
`Uncategorized`, and re-filing it from the web UI teaches the catalog.

To enable it on the server host:

1. Install the [Claude Code CLI](https://code.claude.com/docs/en/setup).
2. Authenticate it non-interactively with **one** of:
   - `ANTHROPIC_API_KEY`: a Claude API key (usage-billed), or
   - `CLAUDE_CODE_OAUTH_TOKEN`: a long-lived token from running `claude setup-token`
     on a machine with a browser (uses a Claude subscription).
3. Put that variable in the service's environment. If the CLI isn't on the
   service's `PATH` (often `/usr/local/bin` isn't under systemd), set `CLAUDE_BIN`
   to its full path.

Don't leave a placeholder `ANTHROPIC_API_KEY` set alongside an OAuth token. The
API key takes precedence, so every classification quietly fails and everything
lands in `Uncategorized`.

## Firmware updates (OTA)

Devices report their version in an `X-Firmware-Version` header on every sync,
and the server offers an update whenever that differs from
`SHOPPING_LIST_FIRMWARE_VERSION` (see [the API](api.md#firmware-updates) and
[how it works](architecture.md#firmware-updates-ota)). To publish a release:

1. Set `FW_VERSION` in `firmware/platformio.ini` and build with `pio run`.
2. Copy the app image, **not** the factory image, into the firmware directory,
   named after the version:

   ```sh
   sudo install -d -o shopping-list /var/lib/papermono-shopping-list/firmware   # first time only
   sudo install -o shopping-list firmware/.pio/build/papermono/firmware.bin \
      /var/lib/papermono-shopping-list/firmware/1.4.0.bin
   ```

3. Set `SHOPPING_LIST_FIRMWARE_VERSION=1.4.0` in the service's environment and
   restart it. Devices pick it up on their next sync, and the server logs each
   sync's reported version so you can watch them arrive.

To roll back, set the variable to an older version whose `.bin` is still in the
directory. To stop offering updates, unset it. The sha256 and size sent to
devices are computed from the file itself, so there's no catalog to keep in
step. A version with no matching file is logged and skipped, and never breaks
list sync.

## Deploy with systemd

```sh
sudo useradd --system --home /var/lib/papermono-shopping-list shopping-list
sudo git clone https://github.com/<you>/papermono-shopping-list /opt/papermono-shopping-list
cd /opt/papermono-shopping-list/server
sudo python3 -m venv .venv && sudo .venv/bin/pip install .

sudo install -m 600 deploy/shopping-list.env.example /etc/papermono-shopping-list.env
sudoedit /etc/papermono-shopping-list.env

sudo cp deploy/shopping-list.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now shopping-list
curl http://localhost:8000/api/health
```

The unit keeps its data in `/var/lib/papermono-shopping-list` (created
automatically) and reads secrets from `/etc/papermono-shopping-list.env`.

To update: `git pull`, `sudo .venv/bin/pip install .`, then
`sudo systemctl restart shopping-list`. The schema is created with
`CREATE TABLE IF NOT EXISTS`, plus a couple of `ALTER TABLE ADD COLUMN`
migrations run on every startup for columns added after the initial release
(see `db.py`); there's no general migration framework beyond that.

## Deploy with Docker

```sh
docker build -t papermono-shopping-list server
docker run -d --name shopping-list -p 8000:8000 -v shopping-list-data:/data papermono-shopping-list
```

The image doesn't include the Claude Code CLI, so auto-sorting is off by default
(`SHOPPING_LIST_CLASSIFIER=none`).

## Security

**The API has no authentication.** Anyone who can reach the port can read and
change the list. That's fine on a home LAN, which is where the device needs to
reach it.

To use the web UI away from home, don't expose the port directly. Publish it
through a reverse proxy or tunnel that authenticates users first, such as
Cloudflare Tunnel with Cloudflare Access, oauth2-proxy, or Caddy or nginx with
an auth module. Keep the device on the plain LAN address. It can't do an
interactive login, and it doesn't need to leave the network.

The web UI sends item and aisle names to the server only. The classifier sends
new item names and your aisle names to Anthropic through the Claude Code CLI;
set `SHOPPING_LIST_CLASSIFIER=none` if you don't want that.

## Backups

Everything is in the one SQLite file. Copy it safely while the server is running
with:

```sh
sqlite3 /var/lib/papermono-shopping-list/shopping.db ".backup shopping-backup.db"
```
