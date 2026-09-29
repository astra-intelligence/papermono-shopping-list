import logging
import sqlite3

from fastapi import APIRouter, Depends, Header, Request, Response

from .. import firmware, repository
from ..db import get_db
from ..models import SyncResponse

router = APIRouter(prefix="/api", tags=["sync"])
log = logging.getLogger(__name__)


@router.get("/sync", response_model=SyncResponse)
def sync(
    request: Request,
    conn: sqlite3.Connection = Depends(get_db),
    x_firmware_version: str | None = Header(default=None),
):
    """Full-state snapshot: everything a client needs to render the list, pre-sorted into aisle
    order, plus the whole catalog for offline autosuggest.

    Deliberately a full replace rather than a delta protocol. The payload is a few KB for a household
    list, so resending everything is simpler and more robust than tracking versions across the
    device's long Wi-Fi-off windows.

    A device that sends `X-Firmware-Version` may also get a `firmware` offer (see firmware.py).
    """
    if x_firmware_version is not None:
        log.info("sync from firmware %s", x_firmware_version)
    body = SyncResponse(
        categories=repository.list_categories(conn),
        items=repository.list_items(conn),
        catalog=repository.list_catalog(conn),
        firmware=firmware.offer_for(request.app.state.settings, x_firmware_version),
    )
    # `firmware` must be absent, not null, when there's no offer, but `response_model_exclude_none`
    # would also strip the legitimate nulls elsewhere (an item's category_id, quantity, unit). So
    # serialise by hand; response_model above still documents the shape in /docs.
    exclude = None if body.firmware else {"firmware"}
    return Response(body.model_dump_json(exclude=exclude), media_type="application/json")
