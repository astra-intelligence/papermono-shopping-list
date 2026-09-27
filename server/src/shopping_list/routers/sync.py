import sqlite3

from fastapi import APIRouter, Depends

from .. import repository
from ..db import get_db
from ..models import SyncResponse

router = APIRouter(prefix="/api", tags=["sync"])


@router.get("/sync", response_model=SyncResponse)
def sync(conn: sqlite3.Connection = Depends(get_db)):
    """Full-state snapshot: everything a client needs to render the list, pre-sorted into aisle
    order, plus the whole catalog for offline autosuggest.

    Deliberately a full replace rather than a delta protocol. The payload is a few KB for a
    household list, so resending everything is simpler and more robust than tracking versions
    across the device's long Wi-Fi-off windows.
    """
    return SyncResponse(
        categories=repository.list_categories(conn),
        items=repository.list_items(conn),
        catalog=repository.list_catalog(conn),
    )
