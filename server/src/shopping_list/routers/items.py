import sqlite3

from fastapi import APIRouter, Depends, HTTPException, Request

from .. import repository
from ..classifier import Classifier
from ..db import get_db
from ..models import ItemCreate, ItemOut, ItemUpdate

router = APIRouter(prefix="/api/items", tags=["items"])


def get_classifier(request: Request) -> Classifier:
    return request.app.state.classifier


def _clean_name(raw: str) -> str:
    # Pydantic's min_length alone lets an all-whitespace name through.
    name = raw.strip()
    if not name:
        raise HTTPException(status_code=400, detail="name cannot be blank")
    return name


def _require_category(conn: sqlite3.Connection, category_id: int) -> None:
    if repository.get_category(conn, category_id) is None:
        raise HTTPException(status_code=400, detail="Unknown category_id")


def _normalize_quantity(quantity: float | None, unit: str | None) -> tuple[float | None, str | None]:
    """A unit only means anything alongside a quantity ("g" on its own is meaningless) - reject
    that combination rather than silently storing a confusing state. An empty/whitespace-only unit
    is treated as "no unit", same as never providing one."""
    unit = unit.strip() if unit else None
    if unit == "":
        unit = None
    if unit is not None and quantity is None:
        raise HTTPException(status_code=400, detail="unit requires a quantity")
    if quantity is not None and quantity <= 0:
        raise HTTPException(status_code=400, detail="quantity must be greater than 0")
    return quantity, unit


@router.post("", response_model=ItemOut, status_code=201)
def create_item(
    body: ItemCreate,
    conn: sqlite3.Connection = Depends(get_db),
    classifier: Classifier = Depends(get_classifier),
):
    name = _clean_name(body.name)
    if body.category_id is not None:
        _require_category(conn, body.category_id)
    quantity, unit = _normalize_quantity(body.quantity, body.unit)
    category_id = repository.resolve_category_id(conn, name, body.category_id, classifier)
    return repository.create_item(conn, name, category_id, quantity, unit)


@router.patch("/{item_id}", response_model=ItemOut)
def update_item(item_id: int, body: ItemUpdate, conn: sqlite3.Connection = Depends(get_db)):
    item = repository.get_item(conn, item_id)
    if item is None:
        raise HTTPException(status_code=404, detail="Item not found")

    name = item.name if body.name is None else _clean_name(body.name)
    if body.category_id is not None:
        _require_category(conn, body.category_id)
        # A manual re-file is the only way to correct a bad auto-classification, so it also updates
        # the catalog; otherwise the next time this name is added it would go straight back to the
        # wrong aisle.
        repository.remember(conn, name, body.category_id)

    # Unlike the fields above, `None` is a legitimate target value for quantity/unit (it means
    # "clear it") - `model_fields_set` is the only way to tell "omitted" from "sent null".
    fields_set = body.model_fields_set
    quantity = item.quantity if "quantity" not in fields_set else body.quantity
    unit = item.unit if "unit" not in fields_set else body.unit
    quantity, unit = _normalize_quantity(quantity, unit)

    return repository.update_item(
        conn,
        item_id,
        name=name,
        category_id=item.category_id if body.category_id is None else body.category_id,
        purchased=item.purchased if body.purchased is None else body.purchased,
        quantity=quantity,
        unit=unit,
    )


@router.delete("/{item_id}", status_code=204)
def delete_item(item_id: int, conn: sqlite3.Connection = Depends(get_db)):
    if repository.get_item(conn, item_id) is None:
        raise HTTPException(status_code=404, detail="Item not found")
    repository.delete_item(conn, item_id)


@router.post("/clear-purchased", status_code=204)
def clear_purchased(conn: sqlite3.Connection = Depends(get_db)):
    repository.clear_purchased(conn)
