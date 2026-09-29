# HTTP API

JSON over HTTP, no authentication (see [server.md](server.md#security)). The
running server also serves interactive OpenAPI docs at `/docs`.

Errors use FastAPI's shape, `{"detail": "..."}`. A `4xx` from a device's queued
edit means "never retry this": the firmware drops the edit (see
[architecture.md](architecture.md#sync-protocol)).

## Objects

```jsonc
// Category (an aisle)
{ "id": 3, "name": "Dairy", "sort_order": 2 }

// Item
{
  "id": 17,
  "name": "Milk",
  "category_id": 3,            // null if its aisle was deleted
  "category_name": "Dairy",    // "Uncategorized" when category_id is null
  "purchased": false,
  "quantity": 2,                // null if not tracked
  "unit": "L",                  // null for a plain count (rendered as "×2"); always null if quantity is null
  "created_at": "2026-09-26T08:12:44.120Z",
  "updated_at": "2026-09-26T08:12:44.120Z"
}

// Catalog entry (a previously-added name)
{ "name": "Milk", "category_id": 3, "category_name": "Dairy" }
```

## Sync

### `GET /api/sync`

Everything a client needs, in one response. The firmware uses nothing else to
read.

```json
{ "categories": [Category], "items": [Item], "catalog": [CatalogEntry] }
```

- `categories` are in walking order (`sort_order`, then name).
- `items` are grouped by aisle in walking order, then alphabetical within an
  aisle. Items with no aisle sort with `Uncategorized`, last. Clients render in
  this order and rely on it for grouping.
- `catalog` is alphabetical.
- `firmware` is only present when the server wants the requesting device on a
  different firmware version. See [Firmware updates](#firmware-updates).

## Firmware updates

The device says which firmware it's running with a request header on
`GET /api/sync`:

```
X-Firmware-Version: 1.3.2
```

The server decides whether that device should update. If so, the sync response
gains one extra top-level field:

```jsonc
{
  "categories": [...], "items": [...], "catalog": [...],
  "firmware": {
    "version": "1.4.0",
    "url": "/api/firmware/1.4.0.bin",   // relative to the server address
    "sha256": "9f2c...",                // 64 lowercase hex characters
    "size": 1310720                     // bytes
  }
}
```

The key is **omitted entirely** (not `null`) when there's nothing to do: the
device already reports the target version, the header is missing or malformed,
no target version is configured, or the binary for it isn't on the server. A
missing binary is logged as a warning and never fails the sync.

The device installs `version` if it differs from what it's running, not only if
it's newer. That is what makes a rollback work: point the server's target back
at an older version.

### `GET /api/firmware/{version}.bin` -> `200`

The application image (`application/octet-stream`) for that version, with
`Content-Length` and an `ETag`. This is the app-only `firmware.bin` that
PlatformIO builds, **not** the merged factory image the web flasher uses. `404`
if the server has no image for that version.

## Items

### `POST /api/items` → `201 Item`

```json
{ "name": "Milk", "category_id": 3, "quantity": 2, "unit": "L" }
```

`name` is 1 to 128 characters and is trimmed; blank names get a `400`.
`category_id` is optional. Leave it out to let the server choose the aisle
([how](architecture.md#how-a-new-item-gets-its-aisle)); that can take a few
seconds if the classifier runs. An unknown `category_id` gets a `400`.
`quantity` and `unit` are both optional; `unit` (max 32 chars) is meaningless
without a `quantity` and gets a `400`, as does a `quantity` that isn't `> 0`.

### `PATCH /api/items/{id}` → `200 Item`

Any subset of:

```json
{ "purchased": true, "name": "Oat milk", "category_id": 4, "quantity": 1, "unit": "L" }
```

Setting `category_id` also updates the catalog, so future adds of this name go
to the new aisle. An unknown item gets a `404`; an unknown `category_id` gets a
`400`. `quantity`/`unit` follow the same validation as create; unlike the other
fields, explicitly sending `"quantity": null` (and `"unit": null`) clears it -
omitting the field entirely leaves it unchanged.

### `DELETE /api/items/{id}` → `204`

`404` if it doesn't exist.

### `POST /api/items/clear-purchased` → `204`

Deletes every purchased item.

## Categories

### `GET /api/categories` → `200 [Category]`

In walking order.

### `POST /api/categories` → `201 Category`

```json
{ "name": "Frozen" }
```

Added at the end of the walk, just before `Uncategorized`. A name that already
exists, compared case-insensitively, gets a `409`.

### `PATCH /api/categories/{id}` → `200 Category`

```json
{ "name": "Freezer", "sort_order": 5 }
```

Both fields are optional. The web UI reorders aisles by swapping the
`sort_order` of two neighbours. Renaming to a name another aisle already has
gets a `409`, and renaming `Uncategorized` gets a `400`.

### `DELETE /api/categories/{id}` → `204`

Its items and catalog entries become uncategorised. Deleting `Uncategorized`
gets a `400`.

## Catalog

### `GET /api/catalog/suggest?q=mil&limit=8` → `200 [CatalogEntry]`

Case-insensitive substring match, with prefix matches first. `limit` is 1 to 25
(default 8), and an empty `q` returns `[]`. Used by the web UI as you type; the
device searches its own copy of the catalog instead.

## Health

### `GET /api/health` → `{"status": "ok"}`
