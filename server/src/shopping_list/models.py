"""Request/response bodies. These are the wire format the firmware parses too (see
firmware/src/sync/model_json.h), so field names are part of the device contract."""

from pydantic import BaseModel, Field


class CategoryOut(BaseModel):
    id: int
    name: str
    sort_order: int


class CategoryCreate(BaseModel):
    name: str = Field(min_length=1, max_length=64)


class CategoryUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=64)
    sort_order: int | None = None


class ItemOut(BaseModel):
    id: int
    name: str
    category_id: int | None
    category_name: str
    purchased: bool
    quantity: float | None
    unit: str | None
    created_at: str
    updated_at: str


class ItemCreate(BaseModel):
    name: str = Field(min_length=1, max_length=128)
    # Omit to let the server pick an aisle (catalog lookup, then the classifier).
    category_id: int | None = None
    quantity: float | None = None
    unit: str | None = Field(default=None, max_length=32)


class ItemUpdate(BaseModel):
    purchased: bool | None = None
    name: str | None = Field(default=None, min_length=1, max_length=128)
    category_id: int | None = None
    # Unlike the fields above, None is a legitimate target value for quantity/unit (it means
    # "clear it"), not just "not sent" - callers must check `model_fields_set` rather than
    # `is None` to tell the two apart. See routers/items.py.
    quantity: float | None = None
    unit: str | None = Field(default=None, max_length=32)


class CatalogEntry(BaseModel):
    name: str
    category_id: int | None
    category_name: str


class FirmwareOffer(BaseModel):
    version: str
    # Relative to the server address the device already has.
    url: str
    sha256: str
    size: int


class SyncResponse(BaseModel):
    categories: list[CategoryOut]
    items: list[ItemOut]
    catalog: list[CatalogEntry]
    # Left out of the JSON entirely (not null) unless the device should update; see routers/sync.py.
    firmware: FirmwareOffer | None = None
