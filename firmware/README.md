# Firmware

PlatformIO project for the M5Stack PaperMono.

```sh
cp src/secrets.example.h src/secrets.h   # Wi-Fi + server URL
pio run -t upload
```

Back up the device's flash before the first flash. See [docs/firmware.md](../docs/firmware.md)
for configuration, backup, flashing from a browser and troubleshooting, and
[docs/architecture.md](../docs/architecture.md#firmware-structure) for how the code is laid out.
