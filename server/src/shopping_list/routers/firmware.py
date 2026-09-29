from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import FileResponse

from .. import firmware

router = APIRouter(prefix="/api", tags=["firmware"])


@router.get("/firmware/{version}.bin", response_class=FileResponse)
def download(version: str, request: Request):
    """The app-only OTA image for `version`. FileResponse supplies Content-Length, ETag and Range."""
    path = firmware.image_path(request.app.state.settings, version)
    if path is None:
        raise HTTPException(status_code=404, detail="no such firmware version")
    return FileResponse(path, media_type="application/octet-stream", filename=f"{version}.bin")
