# Project overview

Household shopping list for the M5Stack PaperMono e-paper device (ESP32-S3, 480x800).
Public open-source repo (GPL-3.0), so keep LAN/host-specific detail out of tracked files.

- `firmware/` - PlatformIO/Arduino C++ (`hal/` hardware, `sync/` server sync + OTA + offline
  store, `ui/` screens and widgets, `main.cpp` loop). Version is `FW_VERSION` in `platformio.ini`;
  bump it for every published build (the server uses it to decide on OTA).
- `server/` - FastAPI + SQLite backend and phone web UI (`src/shopping_list/`; `routers/`,
  `classifier.py` aisle auto-sort via the Claude CLI, `firmware.py` OTA, `flasher/` browser flasher).
  Config is env vars only (`config.py`).
- `docs/` - `architecture.md`, `api.md`, `server.md`, `firmware.md`, `hardware.md` (pin map and
  quirks). Update the relevant doc when changing behaviour.

# Commands

```sh
# Server (from server/)
python3 -m venv .venv && .venv/bin/pip install -e '.[dev]'
SHOPPING_LIST_CLASSIFIER=none .venv/bin/uvicorn shopping_list.main:app --reload
.venv/bin/pytest
.venv/bin/ruff check . && .venv/bin/ruff format --check .   # both run in CI

# Firmware (from firmware/)
cp src/secrets.example.h src/secrets.h   # gitignored: Wi-Fi + server URL
pio run                                  # build; `-t upload` flashes, `pio device monitor` for logs
tools/make_factory_image.sh              # merged factory image (CI checks this works)
```

# Gotchas

- Never commit `firmware/src/secrets.h`. Don't flash or open the serial port of the real device
  without being asked; back up its flash before the first flash (see `docs/firmware.md`).
- Local-only and git-ignored: `deploy.sh`, `DEPLOY.md` (how this install is deployed to a Proxmox
  LXC), `.deploy-secrets.env`. Read `DEPLOY.md` before any deploy task; the production DB holds
  real household data.
- E-paper: use partial refreshes, with a forced full refresh every 10 (see README/`docs/firmware.md`).
