"""OTA firmware offers: which image a device should be running, and where to find it.

Images live in `Settings.firmware_dir` as `<version>.bin`. There's deliberately no catalog: the
sha256 and size are derived from the file, so publishing a release is just copying a file in and
setting the target version.
"""

import hashlib
import logging
import re
from pathlib import Path

from .config import Settings
from .models import FirmwareOffer

log = logging.getLogger(__name__)

# Versions become file names and URL segments, so keep them boring. Must start with an
# alphanumeric, which also rules out "." and "..".
_VERSION_RE = re.compile(r"[0-9A-Za-z][0-9A-Za-z._+-]{0,31}")

# (path, mtime_ns, size) -> sha256, so an image is hashed once, not on every device sync.
_hash_cache: dict[tuple[Path, int, int], str] = {}


def is_valid_version(version: str | None) -> bool:
    return version is not None and _VERSION_RE.fullmatch(version) is not None


def image_path(settings: Settings, version: str) -> Path | None:
    """The image file for `version`, or None if the version is malformed or there's no such file."""
    if not is_valid_version(version):
        return None
    path = settings.firmware_dir / f"{version}.bin"
    return path if path.is_file() else None


def _sha256(path: Path) -> str:
    st = path.stat()
    key = (path.resolve(), st.st_mtime_ns, st.st_size)
    if key not in _hash_cache:
        h = hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        _hash_cache[key] = h.hexdigest()
    return _hash_cache[key]


def offer_for(settings: Settings, reported_version: str | None) -> FirmwareOffer | None:
    """What to offer a device that says it's running `reported_version`.

    None whenever there's nothing to do, including every failure mode: a bad config must never
    break list sync, so problems are logged and swallowed here.
    """
    target = settings.firmware_version
    if target is None or not is_valid_version(reported_version) or reported_version == target:
        return None
    if not is_valid_version(target):
        log.warning("SHOPPING_LIST_FIRMWARE_VERSION %r is not a valid version string", target)
        return None
    path = image_path(settings, target)
    if path is None:
        log.warning(
            "firmware %s is the target version but %s/%s.bin doesn't exist",
            target,
            settings.firmware_dir,
            target,
        )
        return None
    return FirmwareOffer(
        version=target,
        url=f"/api/firmware/{target}.bin",
        sha256=_sha256(path),
        size=path.stat().st_size,
    )
