import logging
import sqlite3

from fastapi import APIRouter, Depends, Header, Request, Response

from .. import firmware, repository, telemetry
from ..db import get_db
from ..models import SyncResponse

router = APIRouter(prefix="/api", tags=["sync"])
log = logging.getLogger(__name__)


def _reading(raw: str | None, low: int, high: int) -> int | None:
    """A device-reported integer, or None if it's missing, malformed or outside [low, high]."""
    try:
        value = int(raw) if raw is not None else None
    except ValueError:
        return None
    return value if value is not None and low <= value <= high else None


@router.get("/sync", response_model=SyncResponse)
def sync(
    request: Request,
    conn: sqlite3.Connection = Depends(get_db),
    x_firmware_version: str | None = Header(default=None),
    # Optional device health from newer firmware (see docs/api.md). Typed as str and checked by hand:
    # a garbled reading must never fail a list sync, so anything implausible is just ignored.
    x_battery_percent: str | None = Header(default=None),
    x_wifi_rssi: str | None = Header(default=None),
    x_free_heap: str | None = Header(default=None),
):
    """Full-state snapshot: everything a client needs to render the list, pre-sorted into aisle
    order, plus the whole catalog for offline autosuggest.

    Deliberately a full replace rather than a delta protocol. The payload is a few KB for a household
    list, so resending everything is simpler and more robust than tracking versions across the
    device's long Wi-Fi-off windows.

    A device that sends `X-Firmware-Version` may also get a `firmware` offer (see firmware.py).
    """
    battery = _reading(x_battery_percent, 0, 100)
    rssi = _reading(x_wifi_rssi, -127, 0)
    free_heap = _reading(x_free_heap, 0, 1 << 30)
    if x_firmware_version is not None:
        log.info(
            "sync from firmware %s (battery %s%%, rssi %s dBm, free heap %s)",
            x_firmware_version,
            battery,
            rssi,
            free_heap,
        )
        telemetry.record_device_health(x_firmware_version, battery, rssi, free_heap)
    telemetry.syncs.add(1, {"firmware.version": x_firmware_version or "unknown"})
    offer = firmware.offer_for(request.app.state.settings, x_firmware_version)
    if offer:
        telemetry.firmware_offers.add(1, {"from": x_firmware_version, "to": offer.version})
    body = SyncResponse(
        categories=repository.list_categories(conn),
        items=repository.list_items(conn),
        catalog=repository.list_catalog(conn),
        firmware=offer,
    )
    # `firmware` must be absent, not null, when there's no offer, but `response_model_exclude_none`
    # would also strip the legitimate nulls elsewhere (an item's category_id, quantity, unit). So
    # serialise by hand; response_model above still documents the shape in /docs.
    exclude = None if body.firmware else {"firmware"}
    return Response(body.model_dump_json(exclude=exclude), media_type="application/json")
