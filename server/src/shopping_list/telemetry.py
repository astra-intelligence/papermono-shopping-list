"""OpenTelemetry traces, metrics and logs, exported over OTLP/HTTP.

Opt-in: nothing is set up unless `OTEL_EXPORTER_OTLP_ENDPOINT` is set (see `config.py`). The rest of
the app talks only to the OpenTelemetry *API* (the `tracer` and instruments below), which is a
no-op without an SDK, so callers never need to check whether telemetry is on.

The SDK is configured entirely from the standard `OTEL_*` environment variables (endpoint, headers,
service name, resource attributes), so nothing here is specific to one backend.
"""

import logging
from importlib.metadata import PackageNotFoundError, version

from fastapi import FastAPI
from opentelemetry import metrics, trace

tracer = trace.get_tracer("shopping_list")
_meter = metrics.get_meter("shopping_list")

items_created = _meter.create_counter(
    "shopping_list.items.created", unit="{item}", description="Items added to the list"
)
items_purchased = _meter.create_counter(
    "shopping_list.items.purchased", unit="{item}", description="Items ticked off (or un-ticked)"
)
classifications = _meter.create_counter(
    "shopping_list.classifier.calls",
    unit="{call}",
    description="Aisle classifications, by outcome (existing, new_aisle, failed, skipped)",
)
classify_duration = _meter.create_histogram(
    "shopping_list.classifier.duration", unit="s", description="Time spent in the classifier"
)
syncs = _meter.create_counter(
    "shopping_list.sync.requests", unit="{request}", description="Device syncs, by firmware version"
)
firmware_offers = _meter.create_counter(
    "shopping_list.firmware.offers", unit="{offer}", description="OTA updates offered during sync"
)
firmware_downloads = _meter.create_counter(
    "shopping_list.firmware.downloads", unit="{download}", description="OTA image downloads started"
)
device_battery = _meter.create_gauge(
    "shopping_list.device.battery", unit="%", description="Battery level last reported by a device"
)
device_rssi = _meter.create_gauge(
    "shopping_list.device.wifi_rssi", unit="dBm", description="Wi-Fi signal last reported by a device"
)
device_free_heap = _meter.create_gauge(
    "shopping_list.device.free_heap", unit="By", description="Free heap last reported by a device"
)

_DEFAULT_SERVICE_NAME = "shopping-list"
_sdk_configured = False


def setup(app: FastAPI) -> None:
    """Install the SDK providers and exporters (once per process), and instrument `app`."""
    global _sdk_configured
    # Imported here so a server that never enables telemetry doesn't pay for the SDK import.
    from opentelemetry.exporter.otlp.proto.http._log_exporter import OTLPLogExporter
    from opentelemetry.exporter.otlp.proto.http.metric_exporter import OTLPMetricExporter
    from opentelemetry.exporter.otlp.proto.http.trace_exporter import OTLPSpanExporter
    from opentelemetry.instrumentation.fastapi import FastAPIInstrumentor
    from opentelemetry.sdk._logs import LoggerProvider, LoggingHandler
    from opentelemetry.sdk._logs.export import BatchLogRecordProcessor
    from opentelemetry.sdk.metrics import MeterProvider
    from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader
    from opentelemetry.sdk.resources import SERVICE_NAME, SERVICE_VERSION, Resource
    from opentelemetry.sdk.trace import TracerProvider
    from opentelemetry.sdk.trace.export import BatchSpanProcessor

    if _sdk_configured:
        FastAPIInstrumentor.instrument_app(app)
        return
    _sdk_configured = True

    try:
        app_version = version("papermono-shopping-list-server")
    except PackageNotFoundError:
        app_version = "unknown"
    # OTEL_SERVICE_NAME / OTEL_RESOURCE_ATTRIBUTES are read by Resource.create and win over these.
    resource = Resource.create({SERVICE_NAME: _DEFAULT_SERVICE_NAME, SERVICE_VERSION: app_version})

    tracer_provider = TracerProvider(resource=resource)
    tracer_provider.add_span_processor(BatchSpanProcessor(OTLPSpanExporter()))
    trace.set_tracer_provider(tracer_provider)

    metrics.set_meter_provider(
        MeterProvider(resource=resource, metric_readers=[PeriodicExportingMetricReader(OTLPMetricExporter())])
    )

    logger_provider = LoggerProvider(resource=resource)
    logger_provider.add_log_record_processor(BatchLogRecordProcessor(OTLPLogExporter()))
    # Attached to the app's own logger only (not root), so uvicorn's access log doesn't double up
    # with the FastAPI request spans. Records pick up the active trace/span ids automatically.
    logging.getLogger("shopping_list").addHandler(LoggingHandler(logger_provider=logger_provider))

    FastAPIInstrumentor.instrument_app(app, tracer_provider=tracer_provider)
    logging.getLogger(__name__).info(
        "OpenTelemetry enabled (service %s %s)", resource.attributes[SERVICE_NAME], app_version
    )


def record_device_health(
    firmware_version: str | None, battery: int | None, rssi: int | None, free_heap: int | None
):
    """Publish what a device reported on sync as gauges, labelled by its firmware version."""
    attrs = {"firmware.version": firmware_version or "unknown"}
    if battery is not None:
        device_battery.set(battery, attrs)
    if rssi is not None:
        device_rssi.set(rssi, attrs)
    if free_heap is not None:
        device_free_heap.set(free_heap, attrs)
