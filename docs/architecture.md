# Architecture

## Components

| Component | Where | Role |
|-----------|-------|------|
| Server | `server/src/shopping_list/` | Source of truth. FastAPI over SQLite; serves the HTTP API and the phone web UI. |
| Web UI | `server/src/shopping_list/{templates,static}/` | Plain HTML/JS page for phones: add, tick, re-file, clear, manage aisles. No build step. |
| Firmware | `firmware/src/` | The PaperMono app. Renders a local copy of the list and syncs it with the server over Wi-Fi. |
| Classifier | `server/src/shopping_list/classifier.py` | Optional. Sorts never-seen item names into aisles via the Claude Code CLI. |

The server is the only thing that writes the database. Both clients talk plain
HTTP/JSON to it; neither talks to the other.

## Data model

Three tables (`server/src/shopping_list/db.py`):

- **categories**: the aisles. `sort_order` is the walking order. There's always
  an `Uncategorized` row that sorts last and can't be renamed or deleted.
- **items**: what's on the list right now: name, aisle, purchased flag, and an
  optional quantity + unit (e.g. `2` `L`, or a plain count with no unit).
- **catalog**: every item name ever added, mapped to the aisle it was last filed
  under. This drives autosuggest and means an item is classified at most once.

Deleting an aisle sets `category_id` to NULL on its items and catalog entries
(`ON DELETE SET NULL`). Those are then reported as `Uncategorized`, and a catalog
entry with no aisle counts as a miss, so the name is classified again next time.

### How a new item gets its aisle

`repository.resolve_category_id` tries, in order:

1. An explicit `category_id` in the request. The device and web UI send one when
   the user picked an autosuggestion, since they already know its aisle.
2. The catalog entry for that name (case-insensitive).
3. The classifier, if one is configured and at least one aisle exists. It either
   names an existing aisle or proposes a new one, which is created at the end of
   the walk.
4. `Uncategorized`.

The result is always written back to the catalog. Re-filing an item from the web
UI also updates its catalog entry, which is how a bad classification gets
corrected for good.

## Sync protocol

The device keeps a full copy of the list and syncs it by replacing everything,
not by exchanging deltas:

```
connect Wi-Fi
for each queued offline edit, in order:      POST /api/items  or  PATCH /api/items/{id}
    2xx       -> done, remove from queue
    4xx       -> the server will never accept it (e.g. item already deleted): drop it
    other     -> network error or 5xx: keep it for next time
GET /api/sync                               -> categories + items + catalog, pre-sorted
disconnect Wi-Fi
re-add any still-queued "add item" edits as local placeholders
save the result to flash
```

**Why a full replace:** the whole payload is a few KB for a household list, and
the device spends most of its time offline. Resending everything is simpler and
more robust than keeping versions consistent across long offline stretches.

**Why push before pull:** otherwise an item ticked just before the sync would be
un-ticked by the snapshot that follows it.

**Why drop on 4xx:** the queue is replayed in order, so one edit the server
will never accept (for example, ticking an item someone else has since cleared)
would otherwise block every edit behind it forever.

### When the device syncs

- At boot, straight after showing the cached list.
- Every hour (`Config::kSyncIntervalMs`).
- Opportunistically on any tap, if the last successful sync is more than 5
  minutes old (`Config::kTapSyncStaleMs`) - otherwise a stale list would just
  sit there for the rest of the hour while someone's actively using the
  device. The header shows "syncing..." while this runs.
- As soon as possible after a local edit (tick, add or quantity change), with
  a shorter Wi-Fi timeout (5 s rather than 15 s) so a tap made out of range
  fails quickly.
- Never while the keyboard, settings or quantity screen is open. The keyboard
  holds pointers into the catalog, which a sync replaces.
- After a failed sync, not again for 30 s, so repeated taps don't each wait out
  the timeout.

Wi-Fi is switched off between syncs. A sync blocks the main loop, so touch isn't
serviced while one runs.

## Offline behaviour

The device reads and writes a local store (`firmware/src/sync/local_store.cpp`):
JSON files on LittleFS holding the last snapshot and the queue of offline edits.
NVS isn't used because it caps a single value at about 4000 bytes, which a catalog
of only about 60 items would exceed.

- **Ticking an item** updates the screen immediately and queues a `PATCH`.
- **Adding an item** adds a grey placeholder under a "PENDING SYNC" heading and
  queues a `POST`. A placeholder can't be ticked until a sync gives it a real
  server id.
- The screen, autosuggest and the queue all survive a reboot.

## Firmware structure

```
firmware/src/
  main.cpp              loop: board upkeep, touch routing, side buttons, sync scheduling
  config.h              tunables; pulls Wi-Fi/server settings from secrets.h (git-ignored)
  model.h               Category / Item / CatalogEntry / PendingAction
  hal/
    bsp.*               power IC, IO expander, rails, battery, power button, LED, frontlight
    epd.*               e-paper refresh policy (partial vs full)
    touch.*             FT6336G touch -> click / swipe events
  sync/
    sync_client.*       Wi-Fi + HTTP sync (the protocol above)
    local_store.*       LittleFS cache + offline queue
    model_json.*        JSON mapping shared by the two, matching the server's models.py
  ui/
    widgets.h           shared drawing / hit-testing primitives
    list_screen.*       the main list
    keyboard.*          on-screen keyboard + autosuggest
    settings_screen.*   frontlight, sync now, status
    quantity_screen.*   set/edit an item's quantity + unit
```

Drivers and overlays are singletons; the list data and the list screen are
owned by `main.cpp`, which passes the data to the screen and the sync client.

## Display refresh policy

E-paper has two relevant update modes (`firmware/src/hal/epd.h`):

| | Partial update | Full refresh |
|--|--|--|
| Waveform | M5GFX `epd_fastest` (1-bit) | M5GFX `epd_quality` |
| Area | A rectangle | Whole panel |
| Looks like | Instant, no flash, lighter text | About 1 s black/white flash, crisp black text |
| Used for | Ticking, scrolling, typing, settings steps | Boot, a sync that changed something, closing the keyboard or settings |

After 10 partial updates in a row (`Config::kMaxPartialRefreshes`), the next one
becomes a full refresh. Long runs of partial updates build up ghosting and DC
imbalance on the panel, and can permanently damage it.

The header bar is drawn as a black-on-white dot pattern rather than a grey fill.
The 1-bit partial waveform pushes greys to black or white, so a grey bar would
look different after every partial update.
