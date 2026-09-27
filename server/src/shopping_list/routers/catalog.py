import sqlite3

from fastapi import APIRouter, Depends, Query

from .. import repository
from ..db import get_db
from ..models import CatalogEntry

router = APIRouter(prefix="/api/catalog", tags=["catalog"])


@router.get("/suggest", response_model=list[CatalogEntry])
def suggest(
    q: str = Query(default="", max_length=64),
    limit: int = Query(default=8, ge=1, le=25),
    conn: sqlite3.Connection = Depends(get_db),
):
    """Live autosuggest for the web UI.

    The device doesn't call this: it filters the copy of the catalog it got from its last
    /api/sync, so typing works while its Wi-Fi is off between syncs.
    """
    if not q.strip():
        return []
    return repository.suggest(conn, q.strip(), limit)
