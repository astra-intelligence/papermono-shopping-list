import hashlib

import pytest
from fastapi.testclient import TestClient

from shopping_list.classifier import NullClassifier
from shopping_list.config import Settings
from shopping_list.main import create_app

IMAGE = b"\xe9" + b"fake-firmware-image" * 100


def _client(tmp_path, version: str | None, images: dict[str, bytes] | None = None):
    fw_dir = tmp_path / "firmware"
    fw_dir.mkdir()
    for name, data in (images or {}).items():
        (fw_dir / f"{name}.bin").write_bytes(data)
    settings = Settings(
        db_path=tmp_path / "test.db",
        classifier="none",
        claude_bin="claude",
        classify_timeout_seconds=1,
        firmware_dir=fw_dir,
        firmware_version=version,
    )
    return TestClient(create_app(settings, classifier=NullClassifier()))


@pytest.fixture
def ota(tmp_path):
    with _client(tmp_path, "1.4.0", {"1.4.0": IMAGE, "1.3.2": b"old"}) as c:
        yield c


def _sync(client, version=None):
    headers = {} if version is None else {"X-Firmware-Version": version}
    return client.get("/api/sync", headers=headers)


def test_offers_update_when_version_differs(ota):
    r = _sync(ota, "1.3.2")
    assert r.status_code == 200
    assert r.json()["firmware"] == {
        "version": "1.4.0",
        "url": "/api/firmware/1.4.0.bin",
        "sha256": hashlib.sha256(IMAGE).hexdigest(),
        "size": len(IMAGE),
    }


def test_offers_a_rollback_to_an_older_target(tmp_path):
    with _client(tmp_path, "1.3.2", {"1.3.2": b"old", "1.4.0": IMAGE}) as c:
        assert _sync(c, "1.4.0").json()["firmware"]["version"] == "1.3.2"


def test_key_is_absent_when_device_is_current(ota):
    assert "firmware" not in _sync(ota, "1.4.0").json()


def test_key_is_absent_without_the_header(ota):
    assert "firmware" not in _sync(ota).json()


@pytest.mark.parametrize("bad", ["", "..", "../x", "1.4.0 beta", "a" * 33, "-1"])
def test_key_is_absent_for_a_malformed_header(ota, bad):
    assert "firmware" not in _sync(ota, bad).json()


def test_key_is_absent_when_no_target_is_configured(tmp_path):
    with _client(tmp_path, None, {"1.4.0": IMAGE}) as c:
        assert "firmware" not in _sync(c, "1.3.2").json()


def test_missing_image_is_skipped_not_an_error(tmp_path, caplog):
    with _client(tmp_path, "1.4.0", {}) as c:
        r = _sync(c, "1.3.2")
    assert r.status_code == 200
    assert "firmware" not in r.json()
    assert "1.4.0.bin doesn't exist" in caplog.text


def test_invalid_target_is_skipped_not_an_error(tmp_path):
    with _client(tmp_path, "../etc/passwd", {}) as c:
        r = _sync(c, "1.3.2")
    assert r.status_code == 200
    assert "firmware" not in r.json()


def test_offer_does_not_strip_legitimate_nulls(ota):
    """The firmware key is dropped by hand, so nulls elsewhere in the payload must survive."""
    cat = ota.post("/api/categories", json={"name": "Dairy"}).json()
    ota.post("/api/items", json={"name": "Milk", "category_id": cat["id"]})
    for version in ("1.3.2", "1.4.0"):
        item = _sync(ota, version).json()["items"][0]
        assert item["quantity"] is None
        assert item["unit"] is None


def test_hash_is_recomputed_when_the_image_changes(ota, tmp_path):
    first = _sync(ota, "1.3.2").json()["firmware"]["sha256"]
    new = IMAGE + b"more"
    (tmp_path / "firmware" / "1.4.0.bin").write_bytes(new)
    assert _sync(ota, "1.3.2").json()["firmware"]["sha256"] == hashlib.sha256(new).hexdigest() != first


def test_download_serves_the_image(ota):
    r = ota.get("/api/firmware/1.4.0.bin")
    assert r.status_code == 200
    assert r.content == IMAGE
    assert r.headers["content-type"] == "application/octet-stream"
    assert r.headers["content-length"] == str(len(IMAGE))
    assert r.headers["etag"]


def test_download_supports_range(ota):
    r = ota.get("/api/firmware/1.4.0.bin", headers={"Range": "bytes=10-19"})
    assert r.status_code == 206
    assert r.content == IMAGE[10:20]


def test_download_unknown_version_is_404(ota):
    assert ota.get("/api/firmware/9.9.9.bin").status_code == 404


def test_download_cannot_escape_the_firmware_dir(ota, tmp_path):
    (tmp_path / "secret.bin").write_bytes(b"nope")
    assert ota.get("/api/firmware/..%2Fsecret.bin").status_code == 404
    assert ota.get("/api/firmware/../secret.bin").status_code == 404
