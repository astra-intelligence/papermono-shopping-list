# Design notes

## Provenance

The firmware started from [MonoMesh](https://github.com/andrecolz/MonoMesh)
(GPL-3.0), a far more complete Meshtastic-compatible firmware for the same
PaperMono hardware. It isn't a fork of the whole project, which is about 19k
lines. Only the pieces that were self-contained and proven on this hardware were
brought over, and everything else was written for a single-screen app.

| This repo | From MonoMesh | How much changed |
|-----------|---------------|------------------|
| `hal/touch.*` | `touch_manager.*` | Unchanged apart from namespace and trimming unused accessors |
| `hal/bsp.*` | `bsp_papermono.*` | Power IC and IO-expander bring-up, battery reading and the power-off path (including the deep-sleep fallback) kept as they were. LoRa control, RTC, the clock/standby low-power modes and buzzer sound modes removed. |
| `hal/epd.*` | `epd_driver.*` | Rewritten around the same M5GFX calls: two operations (partial, full) plus a mandatory full refresh after 10 partials. MonoMesh left that limit off by default. |
| `ui/keyboard.*` | `ui_engine.cpp`'s keyboard | Same key geometry, shift cycling and "push only the input box per keystroke" discipline. Now table-driven, with an autosuggest strip added. |
| `ui/widgets.h` | `views/settings_widgets.h`, `views/view_base.h` | Visual style (cards, dot-screen header, stepper and info rows) kept; everything the two screens don't use removed. |

Everything else is new: the sync client, local store, list and settings screens,
`main.cpp`, and the whole server.

## Decisions

**Wi-Fi only during a sync.** The device's job is to be glanceable and last on
battery. A Wi-Fi association every few minutes costs far less than staying
connected, and the offline queue makes the gaps invisible to the user.

**Full-state sync, not deltas.** See [architecture.md](architecture.md#sync-protocol).
A household list is a few KB, so versioning and merge logic would buy nothing.

**Last write wins.** If the phone and the device both toggle the same item
between syncs, the device's queued edit is applied when it next syncs, and the
snapshot it then fetches is what both show from then on. For a shopping list
that's the right trade-off. There's no conflict UI.

**The server decides aisles; the device just displays them.** Aisle order and
classification live in one place, so the device needs no knowledge of either and
the phone and device can't disagree.

**The catalog stores names, not items.** Clearing purchased items deletes them,
but their names stay in the catalog, so next week's "milk" autosuggests
immediately with its aisle, and classification never repeats.

**LittleFS for the device cache, not NVS.** NVS caps a single value at about
4000 bytes, which a catalog of about 60 items would exceed.

**No RTC use.** The device doesn't show wall-clock time. "Synced 2m ago" comes
from `millis()`, which avoids time zones and NTP entirely.

**Credentials compiled in.** It's the simplest thing that works for a device you
flash yourself. The cost is a rebuild for a Wi-Fi change; see below.

## Bugs found on the way

Kept here because each one explains a line of code that might otherwise look
unnecessary:

- **Use-after-free on sync.** The keyboard's suggestions point into the catalog
  vector, and a sync replaces it. `main.cpp` refuses to sync while the keyboard
  is open.
- **Overlays stuck on screen.** Closing the keyboard any way other than SEND left
  it painted over the list and eating touches. Every overlay close now goes
  through a single full repaint in `handleTouch()`.
- **One bad edit blocked the queue.** Ticking an item the phone had already
  cleared returned 404 forever and blocked every later edit. 4xx responses now
  drop the edit.
- **Pending adds vanished.** A partial replay followed by a fetch overwrote local
  placeholders, which also meant they didn't survive a reboot. Still-queued adds
  are now put back after every fetch.
- **Low-battery cutoff never ran.** It existed, but nothing called it.
- **Frontlight stuck on.** MonoMesh's default brightness was carried over with no
  way to change it. It now defaults to off and can be adjusted in settings.
- **A placeholder `ANTHROPIC_API_KEY` in the service environment** overrode a
  working OAuth token, so every classification silently failed to
  `Uncategorized`.
- **Stale JavaScript on phones.** Static assets had no `Cache-Control`, so
  browsers cached them heuristically. They're now fingerprinted by content hash
  and served with `no-cache`.

## Known limitations

- **Syncs block the UI.** Touch isn't serviced while Wi-Fi connects and requests
  run, which is up to 5 s out of range, or longer if the server is slow. The fix
  is to move sync to a FreeRTOS task on the second core and hand results back to
  the loop.
- **No on-device Wi-Fi setup.** Changing network means rebuilding. A captive
  portal or BLE provisioning, with credentials in NVS, would fix it.
- **A retried add can duplicate.** If a `POST` times out after the server
  committed it, the retry adds the item again. An idempotency key sent by the
  device and checked by the server would fix it.
- **Pending items can't be ticked** until they've synced and have a server id.
- **No authentication** on the server. See [server.md](server.md#security).
- **No light sleep** between syncs. Battery life hasn't been measured.
- **No schema migrations** on the server yet.
- **Only tested on the C153.**
