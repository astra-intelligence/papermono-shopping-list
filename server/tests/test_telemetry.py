from fastapi.testclient import TestClient

from shopping_list import telemetry
from shopping_list.classifier import NullClassifier
from shopping_list.config import Settings, load_settings
from shopping_list.main import create_app


def _settings(tmp_path, **overrides) -> Settings:
    return Settings(
        db_path=tmp_path / "test.db",
        classifier="none",
        claude_bin="claude",
        classify_timeout_seconds=1,
        **overrides,
    )


def test_telemetry_is_off_without_an_endpoint(monkeypatch):
    monkeypatch.delenv("OTEL_EXPORTER_OTLP_ENDPOINT", raising=False)
    assert load_settings().telemetry_enabled is False


def test_telemetry_on_with_endpoint_unless_sdk_disabled(monkeypatch):
    monkeypatch.setenv("OTEL_EXPORTER_OTLP_ENDPOINT", "https://ingestion.eu.bronto.io")
    assert load_settings().telemetry_enabled is True
    monkeypatch.setenv("OTEL_SDK_DISABLED", "true")
    assert load_settings().telemetry_enabled is False


def test_app_does_not_set_up_the_sdk_by_default(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(telemetry, "setup", lambda app: calls.append(app))
    with TestClient(create_app(_settings(tmp_path), classifier=NullClassifier())):
        pass
    assert calls == []


def test_app_sets_up_the_sdk_when_enabled(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(telemetry, "setup", lambda app: calls.append(app))
    with TestClient(create_app(_settings(tmp_path, telemetry_enabled=True), NullClassifier())):
        pass
    assert len(calls) == 1


def test_sync_accepts_device_health_headers(client):
    r = client.get(
        "/api/sync",
        headers={
            "X-Firmware-Version": "1.0.0",
            "X-Battery-Percent": "81",
            "X-Wifi-Rssi": "-60",
            "X-Free-Heap": "123456",
        },
    )
    assert r.status_code == 200


def test_sync_ignores_implausible_device_health_rather_than_failing(client):
    for headers in (
        {"X-Battery-Percent": "101"},
        {"X-Battery-Percent": "abc"},
        {"X-Wifi-Rssi": "10"},
        {"X-Free-Heap": "-5"},
    ):
        assert client.get("/api/sync", headers=headers).status_code == 200


def test_reading_bounds():
    from shopping_list.routers.sync import _reading

    assert _reading("50", 0, 100) == 50
    assert _reading("-60", -127, 0) == -60
    assert _reading(None, 0, 100) is None
    assert _reading("101", 0, 100) is None
    assert _reading("x", 0, 100) is None


def test_a_failing_telemetry_setup_does_not_stop_the_app(tmp_path, monkeypatch):
    def boom(app):
        raise ImportError("no otel installed")

    monkeypatch.setattr(telemetry, "setup", boom)
    with TestClient(create_app(_settings(tmp_path, telemetry_enabled=True), NullClassifier())) as c:
        assert c.get("/api/health").status_code == 200
