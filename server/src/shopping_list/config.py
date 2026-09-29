"""Runtime configuration, read once from environment variables.

Every setting has a default that works for local development, so
`uvicorn shopping_list.main:app` runs with no environment at all.
"""

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    # SQLite database file. Relative paths resolve against the working directory.
    db_path: Path
    # Which classifier sorts brand-new item names into aisles: "claude" (shells out to the Claude
    # Code CLI) or "none" (everything unknown lands in Uncategorized).
    classifier: str
    # Path to the `claude` binary; set this when it isn't on the service's PATH (common under systemd).
    claude_bin: str
    classify_timeout_seconds: float
    # Directory of OTA images, one `<version>.bin` each (the app-only firmware.bin, not the factory image).
    firmware_dir: Path = Path("data/firmware")
    # The version every device should run. None means no OTA is offered.
    firmware_version: str | None = None
    # Export OpenTelemetry traces/metrics/logs. On only when an OTLP endpoint is configured; the
    # endpoint, auth headers and service name are read by the SDK itself from the standard OTEL_* vars.
    telemetry_enabled: bool = False


def load_settings() -> Settings:
    return Settings(
        db_path=Path(os.environ.get("SHOPPING_LIST_DB", "data/shopping.db")),
        classifier=os.environ.get("SHOPPING_LIST_CLASSIFIER", "claude").strip().lower(),
        claude_bin=os.environ.get("CLAUDE_BIN", "claude"),
        classify_timeout_seconds=float(os.environ.get("SHOPPING_LIST_CLASSIFY_TIMEOUT", "30")),
        firmware_dir=Path(os.environ.get("SHOPPING_LIST_FIRMWARE_DIR", "data/firmware")),
        firmware_version=os.environ.get("SHOPPING_LIST_FIRMWARE_VERSION", "").strip() or None,
        telemetry_enabled=bool(
            os.environ.get("OTEL_EXPORTER_OTLP_ENDPOINT", "").strip()
            and os.environ.get("OTEL_SDK_DISABLED", "").strip().lower() != "true"
        ),
    )
